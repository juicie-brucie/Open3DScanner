#pragma once
// Row #179 — the claim channel the live source asks.
//
// The shim does not claim the USB device (row #170: the privileged helper is
// the daemon owner). This channel is the shim's question to the helper: does
// it hold the claim? On macOS it asks the helper's XPC `status` method; on a
// host with no helper it answers "not claimed" with the reason. It never
// asserts a claim on the helper's behalf.
//
// Row #181: the helper is reached through the user LaunchAgent's Mach service.
// The constructor below is the product path; the Apple-only injection
// constructor is the device-free test seam (the fake helper's endpoint).

#include <memory>
#include <string>

#include "live_source.h"

#if defined(__APPLE__)
namespace openscanner {
namespace helper {
class Client;  // helper_xpc.h; held by the test seam only
}  // namespace helper
}  // namespace openscanner
#endif

namespace shim {

class HelperClaimChannel : public ClaimChannel {
public:
    explicit HelperClaimChannel(std::string service = "org.openscanner.helper");
    ~HelperClaimChannel() override;

#if defined(__APPLE__)
    // Test seam (row #181): a channel over an already-connected helper — the
    // fake helper's anonymous endpoint. `label` is what detail lines call the
    // service. The product constructor leaves this null and asks the Mach
    // service on every query.
    HelperClaimChannel(std::unique_ptr<openscanner::helper::Client> client, std::string label);
#endif

    State Query(std::string* detail) override;

    const std::string& service() const { return service_; }

private:
    std::string service_;
#if defined(__APPLE__)
    std::unique_ptr<openscanner::helper::Client> client_;
#endif
};

}  // namespace shim
