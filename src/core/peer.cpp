#include "peer.h"
#include "catalog.h"
#include "fsutil.h"

#include <rtc/rtc.hpp>

#include <cstdio>
#include <sstream>
#include <system_error>

namespace ss {

namespace {
constexpr size_t kChunk = 16 * 1024;              // safe size for every WebRTC stack
constexpr size_t kHighWater = 1024 * 1024;        // stop queueing above this
constexpr size_t kLowWater = 256 * 1024;          // resume below this
constexpr uint64_t kReportEvery = 256 * 1024;     // progress callback granularity
const char *kChannelLabel = "sheetstation";

std::vector<std::string> splitTabs(const std::string &s, size_t maxParts) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (parts.size() + 1 < maxParts) {
        size_t pos = s.find('\t', start);
        if (pos == std::string::npos) break;
        parts.push_back(s.substr(start, pos - start));
        start = pos + 1;
    }
    parts.push_back(s.substr(start));
    return parts;
}

uint32_t parseId(const std::string &s) {
    try {
        return static_cast<uint32_t>(std::stoul(s));
    } catch (...) {
        return 0;
    }
}

uint64_t parseSize(const std::string &s) {
    try {
        return static_cast<uint64_t>(std::stoull(s));
    } catch (...) {
        return 0;
    }
}

std::string sanitizeMessage(std::string s) {
    for (char &c : s)
        if (c == '\t' || c == '\n' || c == '\r') c = ' ';
    return s;
}
} // namespace

uint64_t nextTransferUid() {
    static std::atomic<uint64_t> counter{1};
    return counter++;
}

std::shared_ptr<Peer> Peer::create(const std::string &remoteId, Role role, const Config &config,
                                   Callbacks callbacks) {
    return std::shared_ptr<Peer>(new Peer(remoteId, role, config, std::move(callbacks)));
}

Peer::Peer(const std::string &remoteId, Role role, const Config &config, Callbacks callbacks)
    : m_remoteId(remoteId), m_role(role), m_config(config), m_cb(std::move(callbacks)),
      m_lastActivity(std::chrono::steady_clock::now()) {}

Peer::~Peer() {
    if (m_dc) m_dc->resetCallbacks();
    if (m_pc) {
        m_pc->resetCallbacks();
        m_pc->close();
    }
}

void Peer::start() {
    rtc::Configuration cfg;
    for (const auto &s : m_config.iceServers) {
        if (s.empty()) continue;
        try {
            cfg.iceServers.emplace_back(s);
        } catch (const std::exception &) {
            // a mistyped server is skipped rather than killing the connection
        }
    }

    m_pc = std::make_shared<rtc::PeerConnection>(cfg);
    std::weak_ptr<Peer> weak = shared_from_this();

    m_pc->onGatheringStateChange([weak](rtc::PeerConnection::GatheringState gs) {
        auto self = weak.lock();
        if (!self || gs != rtc::PeerConnection::GatheringState::Complete) return;
        if (self->m_signalSent.exchange(true)) return;
        auto desc = self->m_pc->localDescription();
        if (desc && self->m_cb.onSignal) self->m_cb.onSignal(desc->typeString(), std::string(*desc));
    });

    m_pc->onStateChange([weak](rtc::PeerConnection::State st) {
        auto self = weak.lock();
        if (!self) return;
        using S = rtc::PeerConnection::State;
        if (st == S::Failed) {
            self->failAll("connection failed");
            self->setState(LinkState::Failed);
        } else if (st == S::Closed || st == S::Disconnected) {
            self->failAll("connection closed");
            self->setState(LinkState::Closed);
        }
    });

    if (m_role == Role::Offerer) {
        setupChannel(m_pc->createDataChannel(kChannelLabel));
    } else {
        m_pc->onDataChannel([weak](std::shared_ptr<rtc::DataChannel> dc) {
            if (auto self = weak.lock()) self->setupChannel(dc);
        });
    }
}

void Peer::applyRemoteDescription(const std::string &type, const std::string &sdp) {
    if (!m_pc) return;
    try {
        m_pc->setRemoteDescription(rtc::Description(sdp, type));
        touch();
    } catch (const std::exception &e) {
        failAll(std::string("bad remote description: ") + e.what());
        setState(LinkState::Failed);
    }
}

void Peer::setupChannel(const std::shared_ptr<rtc::DataChannel> &dc) {
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        m_dc = dc;
    }
    std::weak_ptr<Peer> weak = shared_from_this();
    dc->setBufferedAmountLowThreshold(kLowWater);
    dc->onOpen([weak]() {
        if (auto self = weak.lock()) self->onOpen();
    });
    dc->onClosed([weak]() {
        if (auto self = weak.lock()) {
            self->failAll("channel closed");
            self->setState(LinkState::Closed);
        }
    });
    dc->onBufferedAmountLow([weak]() {
        if (auto self = weak.lock()) self->pump();
    });
    dc->onMessage(
        [weak](rtc::binary data) {
            if (auto self = weak.lock()) self->onBinary(data.data(), data.size());
        },
        [weak](rtc::string text) {
            if (auto self = weak.lock()) self->onText(text);
        });
}

void Peer::onOpen() {
    std::vector<std::string> gets;
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        m_open = true;
        for (auto &kv : m_downloads) {
            if (!kv.second->sent) {
                kv.second->sent = true;
                gets.push_back("GET\t" + std::to_string(kv.first) + "\t" + kv.second->name);
            }
        }
    }
    touch();
    setState(LinkState::Connected);
    for (const auto &g : gets) m_dc->send(g);
}

uint64_t Peer::requestFile(const std::string &name) {
    auto d = std::make_unique<Download>();
    d->uid = nextTransferUid();
    d->name = name;
    uint64_t uid = d->uid;
    Events events;
    std::string toSend;
    std::shared_ptr<rtc::DataChannel> dc;
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        d->id = m_nextId++;
        if (!isSafeFileName(name)) {
            events.push_back(eventFor(*d, TransferState::Failed, "unsafe file name"));
        } else {
            events.push_back(eventFor(*d, TransferState::Waiting));
            if (m_open) {
                d->sent = true;
                toSend = "GET\t" + std::to_string(d->id) + "\t" + name;
                dc = m_dc;
            }
            m_downloads[d->id] = std::move(d);
        }
    }
    touch();
    emitEvents(events);
    if (dc && !toSend.empty()) dc->send(toSend);
    return uid;
}

void Peer::onText(const std::string &msg) {
    touch();
    auto parts = splitTabs(msg, 3);
    if (parts.size() < 3) return;
    const std::string &cmd = parts[0];
    uint32_t id = parseId(parts[1]);
    const std::string &arg = parts[2];
    Events events;

    if (cmd == "GET") {
        auto u = std::make_unique<Upload>();
        u->id = id;
        u->uid = nextTransferUid();
        u->name = arg;
        std::string err;
        if (!isSafeFileName(arg)) {
            err = "refused file name";
        } else {
            std::string path = joinPath(m_config.shareDir, arg);
            std::error_code ec;
            if (!fs::is_regular_file(toPath(path), ec)) {
                err = "no such file";
            } else {
                u->size = fs::file_size(toPath(path), ec);
                u->in.open(toPath(path), std::ios::binary);
                if (!u->in) err = "cannot open file";
            }
        }
        if (!err.empty()) {
            events.push_back(eventFor(*u, TransferState::Failed, err));
            emitEvents(events);
            if (m_dc) m_dc->send("ERR\t" + std::to_string(id) + "\t" + err);
            return;
        }
        {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            events.push_back(eventFor(*u, TransferState::Waiting));
            m_uploads.push_back(std::move(u));
        }
        emitEvents(events);
        pump();
        return;
    }

    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        auto it = m_downloads.find(id);
        if (it == m_downloads.end()) return;
        Download &d = *it->second;

        if (cmd == "BEGIN") {
            d.size = parseSize(arg);
            if (!ensureDir(m_config.downloadDir)) {
                events.push_back(eventFor(d, TransferState::Failed, "cannot create download folder"));
                m_downloads.erase(it);
            } else {
                d.partPath = joinPath(m_config.downloadDir, "." + d.name + "." + std::to_string(d.uid) + ".part");
                d.out.open(toPath(d.partPath), std::ios::binary | std::ios::trunc);
                if (!d.out) {
                    events.push_back(eventFor(d, TransferState::Failed, "cannot write download"));
                    m_downloads.erase(it);
                } else {
                    d.begun = true;
                    events.push_back(eventFor(d, TransferState::Running));
                }
            }
        } else if (cmd == "END") {
            d.out.close();
            std::string digest = d.sha.hexDigest();
            std::error_code ec;
            if (!d.begun || d.got != d.size || digest != arg) {
                fs::remove(toPath(d.partPath), ec);
                events.push_back(eventFor(d, TransferState::Failed, "checksum mismatch"));
            } else {
                std::string finalPath = uniquePath(m_config.downloadDir, d.name);
                fs::rename(toPath(d.partPath), toPath(finalPath), ec);
                if (ec) {
                    events.push_back(eventFor(d, TransferState::Failed, "cannot move file: " + ec.message()));
                } else {
                    TransferEvent ev = eventFor(d, TransferState::Done);
                    ev.path = finalPath;
                    events.push_back(ev);
                }
            }
            m_downloads.erase(it);
        } else if (cmd == "ERR") {
            if (d.out.is_open()) {
                d.out.close();
                std::error_code ec;
                fs::remove(toPath(d.partPath), ec);
            }
            events.push_back(eventFor(d, TransferState::Failed, arg));
            m_downloads.erase(it);
        }
    }
    emitEvents(events);
}

void Peer::onBinary(const std::byte *data, size_t len) {
    touch();
    if (len < 4) return;
    uint32_t id = (uint32_t(data[0]) << 24) | (uint32_t(data[1]) << 16) |
                  (uint32_t(data[2]) << 8) | uint32_t(data[3]);
    Events events;
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        auto it = m_downloads.find(id);
        if (it == m_downloads.end()) return;
        Download &d = *it->second;
        if (!d.begun) return;
        const char *payload = reinterpret_cast<const char *>(data + 4);
        size_t n = len - 4;
        d.out.write(payload, static_cast<std::streamsize>(n));
        d.sha.update(payload, n);
        d.got += n;
        if (!d.out) {
            d.out.close();
            std::error_code ec;
            fs::remove(toPath(d.partPath), ec);
            events.push_back(eventFor(d, TransferState::Failed, "disk write failed"));
            m_downloads.erase(it);
        } else if (d.got - d.lastReported >= kReportEvery) {
            d.lastReported = d.got;
            events.push_back(eventFor(d, TransferState::Running));
        }
    }
    emitEvents(events);
}

void Peer::pump() {
    Events events;
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (m_pumping || !m_open || !m_dc) return;
        m_pumping = true;
        std::vector<char> buf(kChunk + 4);
        while (!m_uploads.empty() && m_dc->isOpen() && m_dc->bufferedAmount() < kHighWater) {
            Upload &u = *m_uploads.front();
            if (!u.begun) {
                u.begun = true;
                m_dc->send("BEGIN\t" + std::to_string(u.id) + "\t" + std::to_string(u.size));
                events.push_back(eventFor(u, TransferState::Running));
            }
            buf[0] = char((u.id >> 24) & 0xff);
            buf[1] = char((u.id >> 16) & 0xff);
            buf[2] = char((u.id >> 8) & 0xff);
            buf[3] = char(u.id & 0xff);
            u.in.read(buf.data() + 4, kChunk);
            std::streamsize n = u.in.gcount();
            if (n > 0) {
                u.sha.update(buf.data() + 4, size_t(n));
                u.sent += uint64_t(n);
                m_dc->send(reinterpret_cast<const std::byte *>(buf.data()), size_t(n) + 4);
                if (u.sent - u.lastReported >= kReportEvery) {
                    u.lastReported = u.sent;
                    events.push_back(eventFor(u, TransferState::Running));
                }
            }
            if (n < std::streamsize(kChunk)) { // EOF (or read error)
                if (u.sent == u.size) {
                    m_dc->send("END\t" + std::to_string(u.id) + "\t" + u.sha.hexDigest());
                    events.push_back(eventFor(u, TransferState::Done));
                } else {
                    m_dc->send("ERR\t" + std::to_string(u.id) + "\tread error on sender");
                    events.push_back(eventFor(u, TransferState::Failed, "read error"));
                }
                m_uploads.pop_front();
            }
        }
        m_pumping = false;
    }
    if (!events.empty()) touch();
    emitEvents(events);
}

void Peer::close() {
    failAll("closed");
    if (m_dc) m_dc->close();
    if (m_pc) m_pc->close();
    setState(LinkState::Closed);
}

void Peer::failAll(const std::string &why) {
    Events events;
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        m_open = false;
        for (auto &kv : m_downloads) {
            Download &d = *kv.second;
            if (d.out.is_open()) {
                d.out.close();
                std::error_code ec;
                fs::remove(toPath(d.partPath), ec);
            }
            events.push_back(eventFor(d, TransferState::Failed, why));
        }
        m_downloads.clear();
        for (auto &u : m_uploads) events.push_back(eventFor(*u, TransferState::Failed, why));
        m_uploads.clear();
    }
    emitEvents(events);
}

void Peer::setState(LinkState s) {
    LinkState prev = m_state.exchange(s);
    if (prev == s) return;
    // terminal states stick
    if (prev == LinkState::Failed || (prev == LinkState::Closed && s != LinkState::Failed)) {
        m_state = prev;
        return;
    }
    if (m_cb.onState) m_cb.onState(s);
}

bool Peer::hasActiveTransfers() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return !m_downloads.empty() || !m_uploads.empty();
}

void Peer::touch() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_lastActivity = std::chrono::steady_clock::now();
}

std::chrono::steady_clock::time_point Peer::lastActivity() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_lastActivity;
}

void Peer::emitEvents(Events &events) {
    if (!m_cb.onTransfer) return;
    for (const auto &e : events) m_cb.onTransfer(e);
    events.clear();
}

TransferEvent Peer::eventFor(const Download &d, TransferState st, const std::string &err) const {
    TransferEvent e;
    e.uid = d.uid;
    e.peerId = m_remoteId;
    e.name = d.name;
    e.dir = TransferDir::Download;
    e.state = st;
    e.bytes = d.got;
    e.total = d.size;
    e.error = sanitizeMessage(err);
    return e;
}

TransferEvent Peer::eventFor(const Upload &u, TransferState st, const std::string &err) const {
    TransferEvent e;
    e.uid = u.uid;
    e.peerId = m_remoteId;
    e.name = u.name;
    e.dir = TransferDir::Upload;
    e.state = st;
    e.bytes = u.sent;
    e.total = u.size;
    e.error = sanitizeMessage(err);
    return e;
}

} // namespace ss
