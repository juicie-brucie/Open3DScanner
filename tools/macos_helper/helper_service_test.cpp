// Device-free test of the request dispatcher (row #170).
//
// The XPC listener calls HelperService::Handle for every request; this drives
// that same call with a fake backend, so the protocol's four ops, the unknown
// op refusal and the state each reply reports are pinned without XPC and
// without a device.

#include <cstdio>
#include <memory>
#include <string>

#include "device_claim.h"
#include "helper_service.h"

namespace {

using openscanner::helper::ClaimState;
using openscanner::helper::ClaimStateFromName;
using openscanner::helper::ClaimStateName;
using openscanner::helper::DeviceClaim;
using openscanner::helper::HandleOpName;
using openscanner::helper::HelperService;
using openscanner::helper::Op;
using openscanner::helper::OpFromName;
using openscanner::helper::OpName;
using openscanner::helper::Reply;

int g_checks = 0;
int g_failures = 0;

void Check(bool condition, const std::string& name) {
    ++g_checks;
    if (condition) {
        std::printf("  ok   %s\n", name.c_str());
        return;
    }
    ++g_failures;
    std::printf("  FAIL %s\n", name.c_str());
}

struct FakeClaim : DeviceClaim {
    FakeClaim(int* acquire_count, int* release_count) : acquires(acquire_count), releases(release_count) {}
    const char* Name() const override { return "fake"; }
    bool Acquire(std::string*) override {
        ++*acquires;
        return true;
    }
    void Release() override { ++*releases; }
    int* acquires;
    int* releases;
};

void TestNamesRoundTrip() {
    std::printf("op and state names\n");
    const Op ops[] = {Op::kPing, Op::kClaim, Op::kRelease, Op::kStatus};
    for (Op op : ops) {
        Op parsed = Op::kStatus;
        Check(OpFromName(OpName(op), &parsed) && parsed == op,
              std::string("op name round-trips: ") + OpName(op));
    }
    Check(!OpFromName("nonsense", nullptr), "an unknown op name does not parse");
    Check(!OpFromName("", nullptr), "an empty op name does not parse");

    const ClaimState states[] = {ClaimState::kIdle, ClaimState::kClaiming, ClaimState::kClaimed,
                                 ClaimState::kReleasing, ClaimState::kReleased};
    for (ClaimState state : states) {
        ClaimState parsed = ClaimState::kIdle;
        Check(ClaimStateFromName(ClaimStateName(state), &parsed) && parsed == state,
              std::string("state name round-trips: ") + ClaimStateName(state));
    }
    Check(!ClaimStateFromName("claimed!", nullptr), "an unknown state name does not parse");
}

void TestDispatch() {
    std::printf("dispatch\n");
    int acquires = 0;
    int releases = 0;
    HelperService service(std::make_unique<FakeClaim>(&acquires, &releases), nullptr);

    const Reply ping = service.Handle(Op::kPing);
    Check(ping.ok && ping.detail == "pong", "ping answers pong");
    Check(ping.state == ClaimState::kIdle, "ping reports the state without changing it");

    const Reply status = service.Handle(Op::kStatus);
    Check(status.ok, "status succeeds");
    Check(status.detail.find("backend=fake") != std::string::npos, "status names the backend");

    const Reply claim = service.Handle(Op::kClaim);
    Check(claim.ok && claim.state == ClaimState::kClaimed, "claim reports claimed");
    Check(acquires == 1 && releases == 0, "claim reached the backend");

    const Reply while_claimed = service.Handle(Op::kStatus);
    Check(while_claimed.state == ClaimState::kClaimed, "status follows the claim");

    const Reply release = service.Handle(Op::kRelease);
    Check(release.ok && release.state == ClaimState::kReleased, "release reports released");
    Check(releases == 1, "release reached the backend");

    const Reply quit = service.Stop();
    Check(quit.ok && releases == 1, "Stop releases nothing extra when nothing is held");

    const Reply after_quit = service.Handle(Op::kClaim);
    Check(!after_quit.ok, "a claim after Stop is refused");
}

void TestUnknownOpName() {
    std::printf("unknown op name\n");
    int acquires = 0;
    int releases = 0;
    HelperService service(std::make_unique<FakeClaim>(&acquires, &releases), nullptr);

    const Reply refused = HandleOpName(service, "nonsense");
    Check(!refused.ok, "an unknown op is refused");
    Check(refused.detail == "unknown op: nonsense", "the refusal echoes the name");
    Check(acquires == 0 && releases == 0, "an unknown op never touches the device");

    const Reply ping = HandleOpName(service, "ping");
    Check(ping.ok && ping.detail == "pong", "a known name still dispatches");
}

}  // namespace

int main() {
    TestNamesRoundTrip();
    TestDispatch();
    TestUnknownOpName();

    std::printf("helper_service_test: %d check(s), %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
