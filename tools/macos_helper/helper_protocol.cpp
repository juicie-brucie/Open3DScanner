#include "helper_protocol.h"

namespace openscanner::helper {

const char* ClaimStateName(ClaimState state) {
    switch (state) {
        case ClaimState::kIdle:
            return "idle";
        case ClaimState::kClaiming:
            return "claiming";
        case ClaimState::kClaimed:
            return "claimed";
        case ClaimState::kReleasing:
            return "releasing";
        case ClaimState::kReleased:
            return "released";
    }
    return "unknown";
}

bool ClaimStateFromName(const std::string& name, ClaimState* out) {
    const ClaimState states[] = {ClaimState::kIdle, ClaimState::kClaiming, ClaimState::kClaimed,
                                 ClaimState::kReleasing, ClaimState::kReleased};
    for (ClaimState state : states) {
        if (name == ClaimStateName(state)) {
            if (out) *out = state;
            return true;
        }
    }
    return false;
}

const char* OpName(Op op) {
    switch (op) {
        case Op::kPing:
            return "ping";
        case Op::kClaim:
            return "claim";
        case Op::kRelease:
            return "release";
        case Op::kStatus:
            return "status";
    }
    return "unknown";
}

bool OpFromName(const std::string& name, Op* out) {
    const Op ops[] = {Op::kPing, Op::kClaim, Op::kRelease, Op::kStatus};
    for (Op op : ops) {
        if (name == OpName(op)) {
            if (out) *out = op;
            return true;
        }
    }
    return false;
}

}  // namespace openscanner::helper
