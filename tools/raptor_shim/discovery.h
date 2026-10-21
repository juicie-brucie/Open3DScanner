#pragma once
// GVCP discovery (protocol-spec.md §5).
//
// The spec records the 8-byte DISCOVERY_CMD on UDP 3956 and explicitly does
// not record the daemon's DISCOVERY_ACK (§5, §6.4): the startup window that
// would carry it rotated out of the capture. So the shim recognises the
// command and logs it, and does not fabricate an acknowledgement. A client
// that needs the port map gets it from ob_get_device_enumeration_info, which
// is the path the app already uses.
//
// The listener is off by default: binding 3956 would take discovery traffic
// from a running vendor daemon.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace shim {

std::vector<uint8_t> DiscoveryCommand();

// Exact match on the 8 bytes the spec records; a short or altered datagram is
// not a discovery command.
bool IsDiscoveryCommand(const uint8_t* p, size_t n);

class DiscoveryListener {
public:
    using Log = std::function<void(const std::string&)>;

    DiscoveryListener() = default;
    ~DiscoveryListener();
    DiscoveryListener(const DiscoveryListener&) = delete;
    DiscoveryListener& operator=(const DiscoveryListener&) = delete;

    bool Start(const std::string& bind_address, uint16_t port, Log log, std::string* err);
    void Stop();

    uint64_t commands_seen() const { return commands_seen_; }

private:
    void Loop();

    int fd_ = -1;
    std::atomic<bool> stop_{false};
    std::thread thread_;
    Log log_;
    uint64_t commands_seen_ = 0;
};

}  // namespace shim
