#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace openscanner {

// A minimal, dependency-free WebSocket server for two purposes: broadcast
// binary frames (point-cloud data) and text frames (status/control) to
// whichever UI clients are connected, as fast and simply as possible.
// Deliberately not a general WebSocket implementation — no TLS, no
// fragmentation (incoming frames must be a single, unfragmented frame;
// every real control message is small enough that no browser fragments
// it). It listens on all interfaces (not just loopback) so the UI can be
// reached from another machine on the LAN; there is still no auth, so
// don't run it on an untrusted network.
class WsBroadcastServer {
public:
    // Called from an internal background thread (one per connected
    // client) whenever that client sends a text frame. Must not block
    // and must not call back into this server synchronously (Broadcast*
    // takes a lock the caller may already be inside of, indirectly).
    using TextMessageHandler = std::function<void(const std::string&)>;

    WsBroadcastServer();
    ~WsBroadcastServer();

    WsBroadcastServer(const WsBroadcastServer&) = delete;
    WsBroadcastServer& operator=(const WsBroadcastServer&) = delete;

    // Registers the handler for incoming client text frames (e.g. JSON
    // control commands). Call before Start(); not thread-safe against a
    // concurrent Start().
    void SetTextMessageHandler(TextMessageHandler handler);

    // Serves the UI from `dir` on the same port (a non-WebSocket request
    // gets a static file instead of the 101 handshake). Empty disables it.
    // Call before Start(); not thread-safe against a concurrent Start().
    void SetStaticRoot(const std::string& dir);

    // Starts listening on 0.0.0.0:port (all interfaces) and accepting
    // client connections on a background thread. Returns false if the
    // socket couldn't be bound (port already in use, etc.).
    bool Start(uint16_t port);

    // Stops accepting new connections, closes all client sockets, and
    // joins background threads. Safe to call even if Start() failed.
    void Stop();

    // Sends `data` (raw bytes, becomes one WebSocket binary frame) to
    // every currently-connected client. Thread-safe; cheap no-op if no
    // clients are connected.
    void BroadcastBinary(const uint8_t* data, size_t size);

    // Sends `text` as one WebSocket text frame to every currently-
    // connected client (e.g. a status update). Thread-safe; cheap no-op
    // if no clients are connected.
    void BroadcastText(const std::string& text);

    // Number of currently-connected clients. Thread-safe.
    size_t ConnectedClientCount() const;

    // Per-client broadcast counters (row #163), measured at the send: every
    // SendFrame call is timed and its outcome recorded, so none of these is
    // derived from the frame cadence.
    //
    // This server has no user-space send queue: Broadcast* writes
    // synchronously, under the client list's lock, with a 100 ms SO_SNDTIMEO
    // per client. `queue_depth` is therefore the number of clients whose send
    // is in flight at the instant of the snapshot, and `queue_depth_max` is
    // the peak since Start() — a stalled client shows up as depth 1 held for
    // the timeout. Units are microseconds for the send times.
    struct BroadcastClientStats {
        uint64_t id = 0;             // connection id, 1-based, monotonic
        uint64_t sends = 0;          // frames attempted to this client
        uint64_t bytes = 0;          // payload bytes of the successful sends
        uint64_t send_us_last = 0;   // the most recent send
        uint64_t send_us_total = 0;  // mean = send_us_total / sends
        uint64_t send_us_max = 0;
        uint64_t drops = 0;          // failed sends that removed the client
        uint64_t timeouts = 0;       // drops whose socket error was the send timeout
    };

    struct BroadcastStats {
        uint64_t broadcasts = 0;     // Broadcast* calls that had at least one client
        uint64_t sends = 0;          // per-client frame sends (successful + failed)
        uint64_t bytes = 0;          // payload bytes of the successful sends
        uint64_t send_us_last = 0;
        uint64_t send_us_total = 0;
        uint64_t send_us_max = 0;
        uint64_t drops = 0;
        uint64_t timeouts = 0;
        uint32_t queue_depth = 0;     // clients in a send now
        uint32_t queue_depth_max = 0; // peak since Start()
        size_t connected = 0;
        std::vector<BroadcastClientStats> per_client;  // current clients, connection order
    };

    // A cumulative snapshot since Start(). Thread-safe; it takes the client
    // list's lock, so it can wait behind a send in progress (bounded by the
    // 100 ms send timeout).
    BroadcastStats GetBroadcastStats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace openscanner
