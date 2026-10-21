#pragma once
// The app <-> privileged-helper protocol (row #170, Phase D seed 2).
//
// Architecture and the #97 decision: docs/recomp/FULL-INDEPENDENCE.md §5 L3
// and §6 Phase D. The helper is a launchd/SMAppService daemon that holds the
// device claim; this protocol is deliberately tiny — ping, claim, release,
// status — and carries no frames. Nothing here opens a UVC or HID interface.
//
// Wire shape, XPC dictionary in both directions:
//   request  { "op": <string> }
//   reply    { "ok": <bool>, "state": <string>, "detail": <string> }
// `state` is one of the ClaimStateName() strings. `detail` is for a human
// reading a log; clients must not parse it.

#include <string>

namespace openscanner::helper {

enum class Op {
    kPing,
    kClaim,
    kRelease,
    kStatus,
};

// The claim lifecycle. `Claiming`/`Releasing` are observable states even
// though the skeleton's backends are synchronous: they are what every
// transition log line names.
enum class ClaimState {
    kIdle,
    kClaiming,
    kClaimed,
    kReleasing,
    kReleased,
};

const char* ClaimStateName(ClaimState state);
bool ClaimStateFromName(const std::string& name, ClaimState* out);

const char* OpName(Op op);
bool OpFromName(const std::string& name, Op* out);

struct Reply {
    bool ok = false;
    ClaimState state = ClaimState::kIdle;
    std::string detail;
};

}  // namespace openscanner::helper
