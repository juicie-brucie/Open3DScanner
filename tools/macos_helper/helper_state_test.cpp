// Device-free test of the claim/release state machine (row #170).
//
// Drives the same ClaimMachine the daemon uses, with a fake backend in place
// of the USB device. What the vendor daemon or the USB stack actually does is
// the operator gate's observation; every assertion here is about the machine's
// own behaviour: one release per claim, release on quit and on unplug, a
// refusal that never reports a claim, and a transition log that shows it.

#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "device_claim.h"
#include "helper_state.h"

namespace {

using openscanner::helper::ClaimMachine;
using openscanner::helper::ClaimState;
using openscanner::helper::DeviceClaim;
using openscanner::helper::NullDeviceClaim;
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

bool Has(const std::vector<std::string>& log, const std::string& needle) {
    for (const std::string& line : log) {
        if (line.find(needle) != std::string::npos) return true;
    }
    return false;
}

std::string Dump(const std::vector<std::string>& log) {
    std::string text;
    for (const std::string& line : log) text += "\n    " + line;
    return text;
}

struct FakeClaim : DeviceClaim {
    FakeClaim(int* acquire_count, int* release_count) : acquires(acquire_count), releases(release_count) {}

    const char* Name() const override { return "fake"; }

    bool Acquire(std::string* why) override {
        ++*acquires;
        if (succeed) return true;
        if (why) *why = refusal;
        return false;
    }

    void Release() override { ++*releases; }

    void SignalUnplug() { NotifyUnplug(); }

    int* acquires;
    int* releases;
    bool succeed = true;
    std::string refusal = "device is held by another process";
};

struct Rig {
    Rig() : fake(new FakeClaim(&acquires, &releases)) {
        machine = std::make_unique<ClaimMachine>(std::unique_ptr<DeviceClaim>(fake), [this](const std::string& line) {
            log.push_back(line);
        });
    }

    ClaimMachine& operator*() { return *machine; }
    ClaimMachine* operator->() { return machine.get(); }

    int acquires = 0;
    int releases = 0;
    FakeClaim* fake = nullptr;
    std::vector<std::string> log;
    std::unique_ptr<ClaimMachine> machine;
};

void TestClaimOnStart() {
    std::printf("claim on start\n");
    Rig rig;
    const Reply started = rig->Start();
    Check(started.ok, "Start reports a held claim");
    Check(started.state == ClaimState::kClaimed, "Start leaves the machine claimed");
    Check(rig.acquires == 1, "the backend was asked exactly once");
    Check(rig.releases == 0, "Start releases nothing");
    Check(rig->state() == ClaimState::kClaimed, "state() agrees");

    const Reply again = rig->Start();
    Check(again.ok && rig.acquires == 1, "a second Start does not claim again");

    const Reply explicit_claim = rig->Acquire();
    Check(explicit_claim.ok && rig.acquires == 1, "Acquire while claimed is idempotent");
    Check(explicit_claim.detail == "already claimed", "idempotent Acquire says so");

    Check(Has(rig.log, "claim: idle -> claiming (start)"), "the log names idle -> claiming on start");
    Check(Has(rig.log, "claim: claiming -> claimed (device=fake)"), "the log names claiming -> claimed");
    if (g_failures && !rig.log.empty()) std::printf("  log:%s\n", Dump(rig.log).c_str());
}

void TestReleaseOnQuit() {
    std::printf("release on quit\n");
    Rig rig;
    rig->Start();
    const Reply quit = rig->Shutdown();
    Check(quit.ok && quit.state == ClaimState::kReleased, "Shutdown leaves the machine released");
    Check(rig.releases == 1, "Shutdown releases the claim exactly once");
    const Reply twice = rig->Shutdown();
    Check(twice.ok && rig.releases == 1, "a second Shutdown does not release again");
    Check(Has(rig.log, "claim: claimed -> releasing (quit)"), "the log names claimed -> releasing on quit");
    Check(Has(rig.log, "claim: releasing -> released (quit: release complete)"),
          "the log names releasing -> released on quit");

    const Reply late_claim = rig->Acquire();
    Check(!late_claim.ok && rig.acquires == 1, "a claim after quit is refused");
    Check(late_claim.detail == "helper is shutting down", "the refusal says the helper is shutting down");
    if (g_failures && !rig.log.empty()) std::printf("  log:%s\n", Dump(rig.log).c_str());
}

void TestReleaseOnUnplug() {
    std::printf("release on unplug\n");
    Rig rig;
    rig->Start();
    const Reply unplugged = rig->Unplug();
    Check(unplugged.ok && unplugged.state == ClaimState::kReleased, "Unplug leaves the machine released");
    Check(rig.releases == 1, "Unplug releases the claim exactly once");
    Check(Has(rig.log, "claim: claimed -> releasing (unplug)"), "the log names claimed -> releasing on unplug");

    const Reply quit_after = rig->Shutdown();
    Check(quit_after.ok && rig.releases == 1, "quitting after an unplug does not release again");
    if (g_failures && !rig.log.empty()) std::printf("  log:%s\n", Dump(rig.log).c_str());
}

void TestUnplugWithoutClaim() {
    std::printf("unplug with no claim\n");
    Rig rig;
    const Reply unplugged = rig->Unplug();
    Check(unplugged.ok, "an unplug with no claim is not an error");
    Check(unplugged.detail == "no claim held", "the reply says no claim was held");
    Check(rig.releases == 0, "the backend is not asked to release what it never claimed");
    Check(rig->state() == ClaimState::kIdle, "the state is unchanged");

    rig->Shutdown();
    Check(rig.releases == 0, "quitting an idle helper releases nothing");
    Check(rig->state() == ClaimState::kReleased, "quitting an idle helper still ends released");
}

void TestBackendSignalledUnplug() {
    std::printf("backend-signalled unplug\n");
    Rig rig;
    rig->Start();
    rig.fake->SignalUnplug();
    Check(rig->state() == ClaimState::kReleased, "the machine follows the backend's unplug event");
    Check(rig.releases == 1, "the backend's own signal releases the claim once");
    Check(Has(rig.log, "claim: claimed -> releasing (unplug)"), "the unplug is logged as such");
}

void TestRefusedClaim() {
    std::printf("refused claim\n");
    Rig rig;
    rig.fake->succeed = false;
    const Reply refused = rig->Start();
    Check(!refused.ok, "a refused claim is not reported as held");
    Check(refused.state == ClaimState::kReleased, "a refused claim leaves the machine released");
    Check(refused.detail == "device is held by another process", "the backend's reason is returned verbatim");
    Check(rig.acquires == 1 && rig.releases == 0, "a claim that never succeeded is not released");
    Check(Has(rig.log, "claim: claiming -> released (claim failed: device is held by another process)"),
          "the refusal and its reason are logged");

    rig.fake->succeed = true;
    const Reply retried = rig->Acquire();
    Check(retried.ok && retried.state == ClaimState::kClaimed, "a later claim retries and succeeds");
    Check(rig.acquires == 2, "the retry reached the backend");
    if (g_failures && !rig.log.empty()) std::printf("  log:%s\n", Dump(rig.log).c_str());
}

void TestOneReleasePerClaim() {
    std::printf("one release per claim\n");
    Rig rig;
    rig->Start();
    rig->Release();
    rig->Acquire();
    rig->Release();
    Check(rig.acquires == 2, "two claims reached the backend");
    Check(rig.releases == 2, "two releases, one per claim");
    rig->Release();
    Check(rig.releases == 2, "a release with nothing held adds no backend call");
    Check(Has(rig.log, "claim: released (no claim held, backend not called)"),
          "the no-op release says the backend was not called");
}

void TestDestructorDropsTheClaim() {
    std::printf("destructor drops a held claim\n");
    int acquires = 0;
    int releases = 0;
    {
        ClaimMachine machine(std::make_unique<FakeClaim>(&acquires, &releases), nullptr);
        machine.Start();
        Check(releases == 0, "the claim is held inside the scope");
    }
    Check(acquires == 1 && releases == 1, "teardown releases the claim it held");
}

void TestNullBackendRefuses() {
    std::printf("null backend\n");
    std::vector<std::string> log;
    ClaimMachine machine(std::make_unique<NullDeviceClaim>(),
                         [&log](const std::string& line) { log.push_back(line); });
    const Reply reply = machine.Start();
    Check(!reply.ok, "the null backend never reports a claim");
    Check(reply.state == ClaimState::kReleased, "the machine ends released");
    Check(reply.detail.find("--claim iokit") != std::string::npos,
          "the refusal names the flag that would claim for real");
    Check(Has(log, "claim failed:"), "the refusal is logged");
}

}  // namespace

int main() {
    TestClaimOnStart();
    TestReleaseOnQuit();
    TestReleaseOnUnplug();
    TestUnplugWithoutClaim();
    TestBackendSignalledUnplug();
    TestRefusedClaim();
    TestOneReleasePerClaim();
    TestDestructorDropsTheClaim();
    TestNullBackendRefuses();

    std::printf("helper_state_test: %d check(s), %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
