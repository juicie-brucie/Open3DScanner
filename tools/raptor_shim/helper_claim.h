#pragma once
// Row #179 — the claim channel the live source asks.
//
// The shim does not claim the USB device (row #170: the privileged helper is
// the daemon owner). This channel is the shim's question to the helper: does
// it hold the claim? On macOS it asks the helper's XPC `status` method; on a
// host with no helper it answers "not claimed" with the reason. It never
// asserts a claim on the helper's behalf.

#include <string>

#include "live_source.h"

namespace shim {

class HelperClaimChannel : public ClaimChannel {
public:
    explicit HelperClaimChannel(std::string service = "org.openscanner.helper");

    State Query(std::string* detail) override;

    const std::string& service() const { return service_; }

private:
    std::string service_;
};

}  // namespace shim
