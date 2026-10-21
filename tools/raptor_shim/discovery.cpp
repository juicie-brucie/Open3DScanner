#include "discovery.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace shim {

std::vector<uint8_t> DiscoveryCommand() { return {0x42, 0x11, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01}; }

bool IsDiscoveryCommand(const uint8_t* p, size_t n) {
    const std::vector<uint8_t> command = DiscoveryCommand();
    if (!p || n != command.size()) return false;
    for (size_t i = 0; i < command.size(); ++i) {
        if (p[i] != command[i]) return false;
    }
    return true;
}

DiscoveryListener::~DiscoveryListener() { Stop(); }

bool DiscoveryListener::Start(const std::string& bind_address, uint16_t port, Log log, std::string* err) {
    log_ = std::move(log);
    fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_ < 0) {
        if (err) *err = std::string("socket: ") + std::strerror(errno);
        return false;
    }
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, bind_address.c_str(), &addr.sin_addr) != 1) {
        if (err) *err = "not an IPv4 address: " + bind_address;
        close(fd_);
        fd_ = -1;
        return false;
    }
    if (bind(fd_, (sockaddr*)&addr, sizeof(addr)) != 0) {
        // The usual cause is the vendor daemon already owning 3956. That is a
        // reason to leave it alone, not to fail the shim.
        if (err) *err = "bind " + bind_address + ":" + std::to_string(port) + ": " + std::strerror(errno);
        close(fd_);
        fd_ = -1;
        return false;
    }
    struct timeval tv = {0, 200000};
    setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    stop_.store(false);
    thread_ = std::thread([this] { Loop(); });
    return true;
}

void DiscoveryListener::Stop() {
    stop_.store(true);
    if (thread_.joinable()) thread_.join();
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
}

void DiscoveryListener::Loop() {
    uint8_t buf[2048];
    while (!stop_.load()) {
        const ssize_t n = recv(fd_, buf, sizeof(buf), 0);
        if (n <= 0) continue;  // the receive bound expiring, or a stray error
        if (!IsDiscoveryCommand(buf, (size_t)n)) {
            if (log_) log_("discovery: ignored a " + std::to_string(n) + "-byte non-command datagram");
            continue;
        }
        ++commands_seen_;
        if (log_) {
            // The ACK is not in the spec (§5, §6.4): recognise and log, do not
            // invent a reply.
            log_("discovery: DISCOVERY_CMD seen; no ACK is sent (the ACK shape is not in the spec)");
        }
    }
}

}  // namespace shim
