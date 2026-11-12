// Core self-test: SHA-256 vectors + two in-process peers exchanging files over WebRTC.
// The "sheet" is replaced by a simple queue that hands descriptions across.
#include "catalog.h"
#include "fsutil.h"
#include "peer.h"
#include "sha256.h"

#include <rtc/rtc.hpp>

#include <condition_variable>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <mutex>
#include <queue>
#include <random>
#include <thread>

using namespace ss;

static int failures = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::cerr << "FAIL " << __LINE__ << ": " #cond << std::endl;         \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

static std::string sha(const std::string &s) {
    Sha256 h;
    h.update(s.data(), s.size());
    return h.hexDigest();
}

static std::string fileSha(const std::string &path) {
    std::ifstream in(toPath(path), std::ios::binary);
    Sha256 h;
    char buf[8192];
    while (in.read(buf, sizeof buf) || in.gcount()) h.update(buf, size_t(in.gcount()));
    return h.hexDigest();
}

struct Signal {
    char to;
    std::string type, sdp;
};

int main() {
    // --- SHA-256 known answers
    CHECK(sha("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sha("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sha(std::string(1000000, 'a')) ==
          "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");

    // --- file name safety
    CHECK(isSafeFileName("Moby Dick.epub"));
    CHECK(!isSafeFileName("../etc/passwd"));
    CHECK(!isSafeFileName("a/b.epub"));
    CHECK(!isSafeFileName(".."));

    // --- folders
    fs::path root = fs::temp_directory_path() / "sheetstation-test";
    std::error_code ec;
    fs::remove_all(root, ec);
    std::string shareA = fromPath(root / "a-share"), dlA = fromPath(root / "a-dl");
    std::string shareB = fromPath(root / "b-share"), dlB = fromPath(root / "b-dl");
    ensureDir(shareA);
    ensureDir(shareB);

    // B shares a 5 MB "book" plus a small one; A shares one file too (both directions)
    std::mt19937 rng(42);
    {
        std::ofstream f(toPath(joinPath(shareB, "Big Book.epub")), std::ios::binary);
        for (int i = 0; i < 5 * 1024 * 1024 + 123; ++i) f.put(char(rng() & 0xff));
        std::ofstream g(toPath(joinPath(shareB, "small.txt")), std::ios::binary);
        g << "hello from B";
        std::ofstream h(toPath(joinPath(shareA, "from-a.txt")), std::ios::binary);
        h << "hello from A";
    }
    auto cat = scanShareDir(shareB);
    CHECK(cat.size() == 2);

    rtc::InitLogger(rtc::LogLevel::Warning);

    std::mutex mu;
    std::condition_variable cv;
    std::queue<Signal> signals;
    int done = 0, failed = 0;

    auto makeCb = [&](char other) {
        Peer::Callbacks cb;
        cb.onSignal = [&, other](const std::string &type, const std::string &sdp) {
            std::lock_guard<std::mutex> l(mu);
            signals.push({other, type, sdp});
            cv.notify_all();
        };
        cb.onTransfer = [&](const TransferEvent &e) {
            std::lock_guard<std::mutex> l(mu);
            const char *dir = e.dir == TransferDir::Download ? "down" : "up";
            if (e.state == TransferState::Done) {
                ++done;
                std::cout << "done " << dir << " " << e.name << " " << e.bytes << "/" << e.total << "\n";
            } else if (e.state == TransferState::Failed) {
                ++failed;
                std::cout << "failed " << dir << " " << e.name << ": " << e.error << "\n";
            }
            cv.notify_all();
        };
        return cb;
    };

    Config cfgA{{}, shareA, dlA}; // no STUN: host candidates only (fine on loopback)
    Config cfgB{{}, shareB, dlB};
    auto a = Peer::create("B", Peer::Role::Offerer, cfgA, makeCb('B'));
    auto b = Peer::create("A", Peer::Role::Answerer, cfgB, makeCb('A'));
    b->start();
    a->requestFile("Big Book.epub"); // queued before the link exists
    a->requestFile("small.txt");
    a->requestFile("missing.epub");  // expect a failure
    a->start();

    // Expected: A downloads 2 ok + 1 fail (B sees 2 uploads ok + 1 fail),
    // then B downloads from-a.txt (2 more done).
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    bool askedReverse = false;
    std::unique_lock<std::mutex> lock(mu);
    while (std::chrono::steady_clock::now() < deadline) {
        while (!signals.empty()) {
            Signal s = signals.front();
            signals.pop();
            lock.unlock();
            (s.to == 'B' ? b : a)->applyRemoteDescription(s.type, s.sdp);
            lock.lock();
        }
        if (!askedReverse && b->state() == LinkState::Connected) {
            askedReverse = true;
            lock.unlock();
            b->requestFile("from-a.txt");
            lock.lock();
        }
        if (done >= 6 && failed >= 2) break;
        cv.wait_for(lock, std::chrono::milliseconds(100));
    }
    lock.unlock();

    std::cout << "done=" << done << " failed=" << failed << std::endl;
    CHECK(done == 6);
    CHECK(failed == 2);
    CHECK(fileSha(joinPath(dlA, "Big Book.epub")) == fileSha(joinPath(shareB, "Big Book.epub")));
    CHECK(fileSha(joinPath(dlA, "small.txt")) == sha("hello from B"));
    CHECK(fileSha(joinPath(dlB, "from-a.txt")) == sha("hello from A"));

    // second download of the same name must not overwrite
    std::string again = uniquePath(dlA, "small.txt");
    CHECK(fromPath(toPath(again).filename()) == "small (1).txt");

    a->close();
    b->close();
    a.reset();
    b.reset();
    rtc::Cleanup().wait();

    std::cout << (failures ? "TESTS FAILED" : "ALL TESTS PASSED") << std::endl;
    return failures ? 1 : 0;
}
