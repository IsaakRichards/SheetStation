// SheetStation - one WebRTC link to another SheetStation instance.
//
// Signaling is "non-trickle": each side waits until ICE gathering is complete
// and then hands the whole description (with every candidate inside) to the
// onSignal callback in one piece. That keeps the Google Sheet traffic to one
// offer row and one answer row per connection.
//
// Wire protocol on the single ordered, reliable data channel "sheetstation":
//   text   "GET\t<id>\t<file name>"   ask the other side for a file
//   text   "BEGIN\t<id>\t<size>"      file is coming
//   binary <4-byte big-endian id><bytes>   a chunk
//   text   "END\t<id>\t<sha256 hex>"  file finished
//   text   "ERR\t<id>\t<message>"     request refused / failed
// Binary data a peer receives is always for its own downloads, so ids from the
// two directions never collide.
//
// All callbacks fire on libdatachannel's threads. They are never invoked while
// the Peer's internal lock is held, so it is safe to call back into a Peer.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "sha256.h"

namespace rtc {
class PeerConnection;
class DataChannel;
} // namespace rtc

namespace ss {

struct Config {
    std::vector<std::string> iceServers; // e.g. "stun:stun.example.org:3478"; may be empty
    std::string shareDir;                // files offered to others
    std::string downloadDir;             // where received files land
};

enum class TransferDir { Download, Upload };
enum class TransferState { Waiting, Running, Done, Failed };
enum class LinkState { Negotiating, Connected, Closed, Failed };

struct TransferEvent {
    uint64_t uid = 0; // unique across the whole process
    std::string peerId;
    std::string name;
    TransferDir dir = TransferDir::Download;
    TransferState state = TransferState::Waiting;
    uint64_t bytes = 0;
    uint64_t total = 0;
    std::string path;  // final file path (downloads, when Done)
    std::string error; // when Failed
};

class Peer : public std::enable_shared_from_this<Peer> {
public:
    enum class Role { Offerer, Answerer };

    struct Callbacks {
        // Full local description ready: send {type, sdp} to the remote through the sheet.
        std::function<void(const std::string &type, const std::string &sdp)> onSignal;
        std::function<void(LinkState)> onState;
        std::function<void(const TransferEvent &)> onTransfer;
    };

    static std::shared_ptr<Peer> create(const std::string &remoteId, Role role,
                                        const Config &config, Callbacks callbacks);
    ~Peer();

    // Offerer: creates the data channel, which kicks off negotiation.
    // Answerer: prepares the connection; call applyRemoteDescription with the offer next.
    void start();
    void applyRemoteDescription(const std::string &type, const std::string &sdp);

    // Queue a download; sent as soon as the channel is open. Returns the event uid.
    uint64_t requestFile(const std::string &name);

    void close();

    const std::string &remoteId() const { return m_remoteId; }
    Role role() const { return m_role; }
    LinkState state() const { return m_state.load(); }
    bool hasActiveTransfers() const;
    std::chrono::steady_clock::time_point lastActivity() const;

private:
    Peer(const std::string &remoteId, Role role, const Config &config, Callbacks callbacks);

    struct Download {
        uint32_t id = 0;
        uint64_t uid = 0;
        std::string name;
        std::string partPath;
        std::ofstream out;
        uint64_t size = 0;
        uint64_t got = 0;
        uint64_t lastReported = 0;
        bool begun = false;
        bool sent = false; // GET already sent
        Sha256 sha;
    };
    struct Upload {
        uint32_t id = 0;
        uint64_t uid = 0;
        std::string name;
        std::ifstream in;
        uint64_t size = 0;
        uint64_t sent = 0;
        uint64_t lastReported = 0;
        bool begun = false;
        Sha256 sha;
    };

    using Events = std::vector<TransferEvent>;

    void setupChannel(const std::shared_ptr<rtc::DataChannel> &dc);
    void onOpen();
    void onText(const std::string &msg);
    void onBinary(const std::byte *data, size_t len);
    void pump();
    void setState(LinkState s);
    void failAll(const std::string &why);
    void touch();
    void emitEvents(Events &events);

    TransferEvent eventFor(const Download &d, TransferState st, const std::string &err = {}) const;
    TransferEvent eventFor(const Upload &u, TransferState st, const std::string &err = {}) const;

    std::string m_remoteId;
    Role m_role;
    Config m_config;
    Callbacks m_cb;

    std::shared_ptr<rtc::PeerConnection> m_pc;
    std::shared_ptr<rtc::DataChannel> m_dc;
    std::atomic<LinkState> m_state{LinkState::Negotiating};
    std::atomic<bool> m_signalSent{false};

    mutable std::recursive_mutex m_mutex;
    bool m_open = false;
    uint32_t m_nextId = 1;
    std::map<uint32_t, std::unique_ptr<Download>> m_downloads;
    std::deque<std::unique_ptr<Upload>> m_uploads;
    bool m_pumping = false;
    std::chrono::steady_clock::time_point m_lastActivity;
};

uint64_t nextTransferUid();

} // namespace ss
