// The claim query where no helper exists: the privileged helper (row #170) is
// a macOS daemon, so nothing on another host can hold the claim. The live
// source is therefore refused, with the reason.

#include "helper_claim.h"

#include <utility>

namespace shim {

HelperClaimChannel::HelperClaimChannel(std::string service) : service_(std::move(service)) {}

HelperClaimChannel::~HelperClaimChannel() = default;

ClaimChannel::State HelperClaimChannel::Query(std::string* detail) {
    if (detail) {
        *detail = "the privileged helper (row #170) is macOS-only, so no helper can hold the claim here";
    }
    return State::kNotClaimed;
}

}  // namespace shim
