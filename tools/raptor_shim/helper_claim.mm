// The macOS claim query: the helper's XPC status method (row #170).
//
// A helper that is not running is a refusal, not an error: the live source
// then says "not-configured: the device is not claimed by openscanner-helper".

#include "helper_claim.h"

#if defined(__APPLE__)

#include <utility>

#include "helper_xpc.h"

namespace shim {

HelperClaimChannel::HelperClaimChannel(std::string service) : service_(std::move(service)) {}

ClaimChannel::State HelperClaimChannel::Query(std::string* detail) {
    openscanner::helper::Client client = openscanner::helper::Client::ConnectMachService(service_.c_str());
    if (!client.valid()) {
        if (detail) *detail = "no connection to the helper on " + service_;
        return State::kUnknown;
    }
    std::string error;
    const openscanner::helper::Reply reply = client.Send(openscanner::helper::Op::kStatus, &error);
    if (!error.empty()) {
        if (detail) *detail = "openscanner-helper is not answering on " + service_ + " (" + error + ")";
        return State::kUnknown;
    }
    if (!reply.ok) {
        if (detail) *detail = "the helper refused the status query: " + reply.detail;
        return State::kNotClaimed;
    }
    if (reply.state == openscanner::helper::ClaimState::kClaimed) return State::kClaimedByHelper;
    if (detail) {
        *detail = std::string("openscanner-helper reports ") +
                  openscanner::helper::ClaimStateName(reply.state);
    }
    return State::kNotClaimed;
}

}  // namespace shim

#endif  // __APPLE__
