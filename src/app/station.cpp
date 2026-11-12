#include "station.h"
#include "catalog.h"
#include "sheetclient.h"

#include <QCoreApplication>
#include <algorithm>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>
#include <QSysInfo>
#include <QUrl>
#include <QUuid>

#include <rtc/rtc.hpp>

namespace {
const int kIdlePollMs = 5000;
const int kFastPollMs = 1500;
const int kHelloMs = 60 * 1000;
const int kPeersMs = 20 * 1000;
const qint64 kConnectTimeoutMs = 45 * 1000;
const qint64 kIdleCloseMs = 3 * 60 * 1000;

class FnEvent : public QEvent {
public:
    static QEvent::Type eventType() {
        static int t = QEvent::registerEventType();
        return static_cast<QEvent::Type>(t);
    }
    explicit FnEvent(std::function<void()> f) : QEvent(eventType()), fn(std::move(f)) {}
    std::function<void()> fn;
};

QString q(const std::string &s) { return QString::fromUtf8(s.data(), int(s.size())); }
std::string s(const QString &q) {
    QByteArray b = q.toUtf8();
    return std::string(b.constData(), size_t(b.size()));
}

QString defaultDir(QStandardPaths::StandardLocation loc, const QString &sub) {
    QString base = QStandardPaths::writableLocation(loc);
    if (base.isEmpty()) base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(base).filePath(sub);
}

QString stateName(ss::LinkState st) {
    switch (st) {
    case ss::LinkState::Negotiating: return QStringLiteral("connecting");
    case ss::LinkState::Connected: return QStringLiteral("connected");
    case ss::LinkState::Closed: return QStringLiteral("idle");
    case ss::LinkState::Failed: return QStringLiteral("failed");
    }
    return QStringLiteral("idle");
}
} // namespace

Station::Station(QObject *parent) : QObject(parent), m_sheet(new SheetClient(this)) {
    rtc::InitLogger(rtc::LogLevel::Warning);
    loadSettings();
    m_sheet->setUrl(QUrl(m_scriptUrl.trimmed()));
    m_lastSignal.start();

    connect(&m_pollTimer, &QTimer::timeout, this, &Station::poll);
    connect(&m_helloTimer, &QTimer::timeout, this, &Station::sendHello);
    connect(&m_peersTimer, &QTimer::timeout, this, &Station::fetchPeers);
    connect(&m_houseTimer, &QTimer::timeout, this, &Station::housekeeping);
    m_transfersDirty.setSingleShot(true);
    m_transfersDirty.setInterval(200);
    connect(&m_transfersDirty, &QTimer::timeout, this, &Station::transfersChanged);

    m_pollTimer.start(kIdlePollMs);
    m_helloTimer.start(kHelloMs);
    m_peersTimer.start(kPeersMs);
    m_houseTimer.start(2000);
    QTimer::singleShot(0, this, &Station::refresh);
}

Station::~Station() {
    for (auto &l : m_links)
        if (l.peer) l.peer->close();
    m_links.clear();
}

// ------------------------------------------------------------------ settings

void Station::loadSettings() {
    QSettings st;
    m_deviceId = st.value(QStringLiteral("deviceId")).toString();
    if (m_deviceId.isEmpty()) {
        m_deviceId = QUuid::createUuid().toString().remove(QLatin1Char('{')).remove(QLatin1Char('}'));
        st.setValue(QStringLiteral("deviceId"), m_deviceId);
    }
    QString host = QSysInfo::machineHostName();
    if (host.isEmpty() || host == QLatin1String("localhost")) host = QSysInfo::prettyProductName();
    m_scriptUrl = st.value(QStringLiteral("scriptUrl")).toString();
    m_deviceName = st.value(QStringLiteral("deviceName"), host).toString();
    m_shareDir = st.value(QStringLiteral("shareDir"),
                          defaultDir(QStandardPaths::DocumentsLocation, QStringLiteral("SheetStation/Shared"))).toString();
    m_downloadDir = st.value(QStringLiteral("downloadDir"),
                             defaultDir(QStandardPaths::DownloadLocation, QStringLiteral("SheetStation"))).toString();
    // Google's free public STUN server: lets devices learn their public address so they
    // can connect across different home networks. No file data ever goes through it.
    // Clear the field in Settings to stay LAN / IPv6 only.
    m_stunServers = st.value(QStringLiteral("stunServers"), QStringLiteral("stun.l.google.com:19302")).toString();
    QDir().mkpath(m_shareDir);
    QDir().mkpath(m_downloadDir);
}

void Station::saveSettings() {
    QSettings st;
    st.setValue(QStringLiteral("scriptUrl"), m_scriptUrl);
    st.setValue(QStringLiteral("deviceName"), m_deviceName);
    st.setValue(QStringLiteral("shareDir"), m_shareDir);
    st.setValue(QStringLiteral("downloadDir"), m_downloadDir);
    st.setValue(QStringLiteral("stunServers"), m_stunServers);
}

void Station::setScriptUrl(const QString &v) {
    if (v == m_scriptUrl) return;
    m_scriptUrl = v.trimmed();
    m_sheet->setUrl(QUrl(m_scriptUrl));
    saveSettings();
    emit settingsChanged();
    refresh();
}

void Station::setDeviceName(const QString &v) {
    if (v == m_deviceName) return;
    m_deviceName = v.trimmed();
    saveSettings();
    emit settingsChanged();
    sendHello();
}

void Station::setShareDir(const QString &v) {
    if (v == m_shareDir) return;
    m_shareDir = v.trimmed();
    QDir().mkpath(m_shareDir);
    saveSettings();
    emit settingsChanged();
    sendHello();
}

void Station::setDownloadDir(const QString &v) {
    if (v == m_downloadDir) return;
    m_downloadDir = v.trimmed();
    QDir().mkpath(m_downloadDir);
    saveSettings();
    emit settingsChanged();
}

void Station::setStunServers(const QString &v) {
    if (v == m_stunServers) return;
    m_stunServers = v.trimmed();
    saveSettings();
    emit settingsChanged();
}

QString Station::platform() const {
#if defined(SHEETSTATION_SAILFISH)
    return QStringLiteral("sailfish");
#elif defined(Q_OS_ANDROID)
    return QStringLiteral("android");
#elif defined(Q_OS_WIN)
    return QStringLiteral("windows");
#elif defined(Q_OS_LINUX)
    return QStringLiteral("linux");
#else
    return QStringLiteral("other");
#endif
}

// ------------------------------------------------------------------ helpers

void Station::post(std::function<void()> fn) { QCoreApplication::postEvent(this, new FnEvent(std::move(fn))); }

void Station::customEvent(QEvent *e) {
    if (e->type() == FnEvent::eventType()) static_cast<FnEvent *>(e)->fn();
}

void Station::setStatus(const QString &st, bool ok) {
    if (st == m_status && ok == m_sheetOk) return;
    m_status = st;
    m_sheetOk = ok;
    emit statusChanged();
}

QString Station::formatSize(double bytes) const {
    const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    int u = 0;
    while (bytes >= 1024.0 && u < 4) {
        bytes /= 1024.0;
        ++u;
    }
    return QString::number(bytes, 'f', u == 0 ? 0 : 1) + QLatin1Char(' ') + QLatin1String(units[u]);
}

bool Station::openPath(const QString &path) { return QDesktopServices::openUrl(QUrl::fromLocalFile(path)); }

QString Station::peerName(const QString &peerId) const {
    for (const QVariant &v : m_rawPeers) {
        QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("id")).toString() == peerId) return m.value(QStringLiteral("name")).toString();
    }
    return peerId.left(8);
}

// ------------------------------------------------------------------ sheet traffic

void Station::refresh() {
    sendHello();
    fetchPeers();
    poll();
}

void Station::sendHello() {
    if (!configured()) {
        setStatus(tr("Set the Apps Script URL in Settings"), false);
        return;
    }
    QJsonArray cat;
    QVariantList mine;
    for (const auto &f : ss::scanShareDir(s(m_shareDir))) {
        QJsonObject o;
        o.insert(QStringLiteral("n"), q(f.name));
        o.insert(QStringLiteral("s"), double(f.size));
        cat.append(o);
        QVariantMap m;
        m.insert(QStringLiteral("name"), q(f.name));
        m.insert(QStringLiteral("size"), double(f.size));
        mine.append(m);
    }
    if (mine != m_myFiles) {
        m_myFiles = mine;
        emit myFilesChanged();
    }
    QJsonObject args;
    args.insert(QStringLiteral("id"), m_deviceId);
    args.insert(QStringLiteral("name"), m_deviceName);
    args.insert(QStringLiteral("platform"), platform());
    args.insert(QStringLiteral("catalog"), cat);
    m_sheet->call(QStringLiteral("hello"), args, [this](bool ok, const QJsonObject &, const QString &err) {
        if (ok)
            setStatus(tr("Online"), true);
        else
            setStatus(tr("Sheet error: %1").arg(err), false);
    });
}

void Station::fetchPeers() {
    if (!configured()) return;
    QJsonObject args;
    args.insert(QStringLiteral("id"), m_deviceId);
    m_sheet->call(QStringLiteral("peers"), args, [this](bool ok, const QJsonObject &r, const QString &err) {
        if (!ok) {
            setStatus(tr("Sheet error: %1").arg(err), false);
            return;
        }
        m_rawPeers = r.value(QStringLiteral("peers")).toArray().toVariantList();
        rebuildPeersList();
    });
}

void Station::poll() {
    if (!configured() || m_pollBusy) return;
    m_pollBusy = true;
    QJsonObject args;
    args.insert(QStringLiteral("id"), m_deviceId);
    m_sheet->call(QStringLiteral("poll"), args, [this](bool ok, const QJsonObject &r, const QString &err) {
        m_pollBusy = false;
        if (!ok) {
            setStatus(tr("Sheet error: %1").arg(err), false);
            return;
        }
        setStatus(tr("Online"), true);
        const QJsonArray msgs = r.value(QStringLiteral("messages")).toArray();
        for (const QJsonValue &v : msgs) {
            QJsonObject m = v.toObject();
            handleMessage(m.value(QStringLiteral("from")).toString(), m.value(QStringLiteral("type")).toString(),
                          m.value(QStringLiteral("payload")).toString());
        }
        updatePollInterval();
    });
}

void Station::updatePollInterval() {
    bool fast = m_lastSignal.elapsed() < 60000;
    for (const auto &l : m_links)
        if (l.peer && l.peer->state() == ss::LinkState::Negotiating) fast = true;
    int want = fast ? kFastPollMs : kIdlePollMs;
    if (m_pollTimer.interval() != want) m_pollTimer.setInterval(want);
}

void Station::housekeeping() {
    QStringList drop;
    auto now = std::chrono::steady_clock::now();
    for (auto it = m_links.begin(); it != m_links.end(); ++it) {
        Link &l = it.value();
        if (!l.peer) {
            drop << it.key();
            continue;
        }
        ss::LinkState st = l.peer->state();
        if (st == ss::LinkState::Negotiating && l.age.elapsed() > kConnectTimeoutMs) {
            emit notify(tr("Could not reach %1 directly. A strict network (campus Wi-Fi, mobile data) may be blocking the link.")
                            .arg(peerName(it.key())));
            drop << it.key();
        } else if (st == ss::LinkState::Connected && !l.peer->hasActiveTransfers() &&
                   std::chrono::duration_cast<std::chrono::milliseconds>(now - l.peer->lastActivity()).count() >
                       kIdleCloseMs) {
            drop << it.key();
        } else if (st == ss::LinkState::Closed || st == ss::LinkState::Failed) {
            drop << it.key();
        }
    }
    for (const QString &id : drop) dropLink(id);
    if (!drop.isEmpty()) rebuildPeersList();
    updatePollInterval();
}

// ------------------------------------------------------------------ WebRTC links

Station::Link &Station::createLink(const QString &peerId, ss::Peer::Role role) {
    dropLink(peerId);
    Link &l = m_links[peerId];
    l.gen = m_nextGen++;
    l.age.start();
    const quint64 gen = l.gen;

    ss::Peer::Callbacks cb;
    cb.onSignal = [this, peerId, gen](const std::string &type, const std::string &sdp) {
        QString t = q(type), p = q(sdp);
        post([this, peerId, gen, t, p]() {
            auto it = m_links.find(peerId);
            if (it == m_links.end() || it->gen != gen) return;
            QJsonObject args;
            args.insert(QStringLiteral("from"), m_deviceId);
            args.insert(QStringLiteral("to"), peerId);
            args.insert(QStringLiteral("type"), t);
            args.insert(QStringLiteral("payload"), p);
            m_sheet->call(QStringLiteral("send"), args, [this](bool ok, const QJsonObject &, const QString &err) {
                if (!ok) emit notify(tr("Could not post connection offer: %1").arg(err));
            });
            m_lastSignal.restart();
            updatePollInterval();
        });
    };
    cb.onState = [this, peerId, gen](ss::LinkState st) {
        post([this, peerId, gen, st]() { onLinkState(peerId, gen, st); });
    };
    cb.onTransfer = [this](const ss::TransferEvent &e) {
        post([this, e]() { onTransfer(e); });
    };

    ss::Config cfg;
    cfg.shareDir = s(m_shareDir);
    cfg.downloadDir = s(m_downloadDir);
    for (QString server : m_stunServers.split(QLatin1Char(','))) {
        server = server.trimmed();
        if (server.isEmpty()) continue;
        if (!server.startsWith(QLatin1String("stun:"), Qt::CaseInsensitive) &&
            !server.startsWith(QLatin1String("turn:"), Qt::CaseInsensitive) &&
            !server.startsWith(QLatin1String("turns:"), Qt::CaseInsensitive))
            server = QStringLiteral("stun:") + server;
        cfg.iceServers.push_back(s(server));
    }
    l.peer = ss::Peer::create(s(peerId), role, cfg, cb);
    l.peer->start();
    rebuildPeersList();
    return l;
}

void Station::dropLink(const QString &peerId) {
    auto it = m_links.find(peerId);
    if (it == m_links.end()) return;
    std::shared_ptr<ss::Peer> p = it->peer;
    m_links.erase(it);
    if (p) p->close();
}

void Station::download(const QString &peerId, const QString &fileName) {
    if (peerId.isEmpty() || fileName.isEmpty()) return;
    auto it = m_links.find(peerId);
    bool usable = it != m_links.end() && it->peer &&
                  (it->peer->state() == ss::LinkState::Connected || it->peer->state() == ss::LinkState::Negotiating);
    Link *l = usable ? &it.value() : &createLink(peerId, ss::Peer::Role::Offerer);
    quint64 uid = l->peer->requestFile(s(fileName));
    if (l->peer->state() == ss::LinkState::Negotiating) l->requested.append(qMakePair(uid, fileName));
    m_lastSignal.restart();
    updatePollInterval();
    if (!usable) poll();
}

void Station::handleMessage(const QString &from, const QString &type, const QString &payload) {
    if (from.isEmpty() || from == m_deviceId) return;
    if (type == QLatin1String("offer")) {
        QList<QPair<quint64, QString>> handOver;
        auto it = m_links.find(from);
        if (it != m_links.end() && it->peer && it->peer->role() == ss::Peer::Role::Offerer &&
            it->peer->state() == ss::LinkState::Negotiating) {
            // Both sides dialled at once. The smaller id keeps its offer; the other answers.
            if (m_deviceId < from) return;
            handOver = it->requested;
            for (const auto &r : handOver) {
                m_suppressed.insert(r.first);
                m_transfers.remove(r.first);
                m_transferOrder.removeAll(r.first);
            }
        }
        Link &l = createLink(from, ss::Peer::Role::Answerer);
        l.peer->applyRemoteDescription(s(type), s(payload));
        for (const auto &r : handOver) l.peer->requestFile(s(r.second));
        m_lastSignal.restart();
        scheduleTransfersChanged();
    } else if (type == QLatin1String("answer")) {
        auto it = m_links.find(from);
        if (it == m_links.end() || !it->peer || it->peer->role() != ss::Peer::Role::Offerer ||
            it->peer->state() != ss::LinkState::Negotiating)
            return;
        it->peer->applyRemoteDescription(s(type), s(payload));
    }
}

void Station::onLinkState(const QString &peerId, quint64 gen, ss::LinkState st) {
    auto it = m_links.find(peerId);
    if (it == m_links.end() || it->gen != gen) return;
    if (st == ss::LinkState::Connected) it->requested.clear();
    if (st == ss::LinkState::Failed) emit notify(tr("Connection to %1 failed").arg(peerName(peerId)));
    rebuildPeersList();
}

void Station::onTransfer(const ss::TransferEvent &e) {
    if (m_suppressed.contains(e.uid)) {
        if (e.state == ss::TransferState::Failed || e.state == ss::TransferState::Done) m_suppressed.remove(e.uid);
        return;
    }
    QVariantMap m = m_transfers.value(e.uid);
    if (m.isEmpty()) m_transferOrder.prepend(e.uid);
    const QString pid = q(e.peerId);
    m.insert(QStringLiteral("uid"), double(e.uid));
    m.insert(QStringLiteral("peerId"), pid);
    m.insert(QStringLiteral("peerName"), peerName(pid));
    m.insert(QStringLiteral("name"), q(e.name));
    m.insert(QStringLiteral("direction"), e.dir == ss::TransferDir::Download ? QStringLiteral("down") : QStringLiteral("up"));
    const char *states[] = {"waiting", "running", "done", "failed"};
    m.insert(QStringLiteral("state"), QLatin1String(states[int(e.state)]));
    m.insert(QStringLiteral("bytes"), double(e.bytes));
    m.insert(QStringLiteral("total"), double(e.total));
    m.insert(QStringLiteral("progress"), e.total ? double(e.bytes) / double(e.total) : (e.state == ss::TransferState::Done ? 1.0 : 0.0));
    if (!e.error.empty()) m.insert(QStringLiteral("error"), q(e.error));
    if (!e.path.empty()) m.insert(QStringLiteral("path"), q(e.path));
    m_transfers.insert(e.uid, m);

    if (e.state == ss::TransferState::Done && e.dir == ss::TransferDir::Download)
        emit notify(tr("Received %1").arg(q(e.name)));
    else if (e.state == ss::TransferState::Failed && e.dir == ss::TransferDir::Download)
        emit notify(tr("%1 failed: %2").arg(q(e.name), q(e.error)));
    scheduleTransfersChanged();
}

void Station::scheduleTransfersChanged() {
    if (!m_transfersDirty.isActive()) m_transfersDirty.start();
}

QVariantList Station::transfers() const {
    QVariantList out;
    for (quint64 uid : m_transferOrder) out.append(m_transfers.value(uid));
    return out;
}

int Station::activeTransfers() const {
    int n = 0;
    for (const QVariantMap &m : m_transfers) {
        QString st = m.value(QStringLiteral("state")).toString();
        if (st == QLatin1String("waiting") || st == QLatin1String("running")) ++n;
    }
    return n;
}

void Station::clearFinished() {
    QList<quint64> keep;
    for (quint64 uid : m_transferOrder) {
        QString st = m_transfers.value(uid).value(QStringLiteral("state")).toString();
        if (st == QLatin1String("done") || st == QLatin1String("failed"))
            m_transfers.remove(uid);
        else
            keep.append(uid);
    }
    m_transferOrder = keep;
    emit transfersChanged();
}

void Station::rebuildPeersList() {
    QVariantList out;
    for (const QVariant &v : m_rawPeers) {
        QVariantMap m = v.toMap();
        QString id = m.value(QStringLiteral("id")).toString();
        QVariantList files;
        for (const QVariant &f : m.value(QStringLiteral("catalog")).toList()) {
            QVariantMap fm = f.toMap();
            QVariantMap e;
            e.insert(QStringLiteral("name"), fm.value(QStringLiteral("n")).toString());
            e.insert(QStringLiteral("size"), fm.value(QStringLiteral("s")).toDouble());
            files.append(e);
        }
        m.insert(QStringLiteral("files"), files);
        m.remove(QStringLiteral("catalog"));
        auto it = m_links.constFind(id);
        m.insert(QStringLiteral("link"), (it != m_links.constEnd() && it->peer) ? stateName(it->peer->state())
                                                                               : QStringLiteral("idle"));
        out.append(m);
    }
    // online devices first, then by name
    std::stable_sort(out.begin(), out.end(), [](const QVariant &a, const QVariant &b) {
        QVariantMap x = a.toMap(), y = b.toMap();
        bool ox = x.value(QStringLiteral("online")).toBool(), oy = y.value(QStringLiteral("online")).toBool();
        if (ox != oy) return ox;
        return x.value(QStringLiteral("name")).toString().compare(y.value(QStringLiteral("name")).toString(),
                                                                  Qt::CaseInsensitive) < 0;
    });
    m_peersList = out;
    emit peersChanged();
}
