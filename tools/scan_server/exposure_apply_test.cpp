// Hardware-free test for the exposure/laser apply path (row #153).
//
// The decision is the whole point of the row: the config is a setter that
// "pushes to the device" (docs/recon/findings/scan-live-data.md §18), so it
// is pushed when the value changes — and only then. The live path calls the
// same helper the test drives here, with the vendor call replaced by a
// recorder, so the wiring is checked without the SDK.
//
// The cases the live path depends on:
//
//   * the first application happens (manual mode, nothing pushed yet);
//   * the same value is not pushed again (the tick's skip);
//   * a changed value is pushed (the slider / a property SET);
//   * auto mode never pushes (the device's own auto-exposure owns the value);
//   * returning to manual mode pushes again, even the same number, because
//     the auto period in between may have moved the device on;
//   * a resume invalidates what was pushed (the SDK re-runs its scan-mode
//     setup), so the next manual tick pushes it again;
//   * a refused push is retried (the device did not accept the value).
#include "exposure_apply.h"

#include <cstdio>
#include <string>
#include <vector>

namespace exposure = openscanner::scan_server;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    if (condition) return;
    std::fprintf(stderr, "[FAIL] %s\n", what.c_str());
    ++g_failures;
}

// Records what the live path would have pushed, and how many times.
struct Recorder {
    std::vector<int> pushed;
    bool accept = true;

    bool Push(int value) {
        pushed.push_back(value);
        return accept;
    }
    int count() const { return static_cast<int>(pushed.size()); }
};

void CheckFirstApplication() {
    exposure::ExposureApplyTracker tracker;
    Recorder rec;
    const bool pushed = exposure::ApplyExposureIfChanged(&tracker, /*auto_mode=*/false, 900,
                                                           [&](int v) { return rec.Push(v); });
    Check(pushed, "the first manual value is pushed");
    Check(rec.count() == 1 && rec.pushed[0] == 900, "the requested value is what is pushed");
    Check(tracker.pushed(), "the tracker remembers the push");
    Check(tracker.pushed_value() == 900, "the tracker remembers the value");
}

void CheckSkipWhenUnchanged() {
    exposure::ExposureApplyTracker tracker;
    Recorder rec;
    auto tick = [&](bool auto_mode, int value) {
        return exposure::ApplyExposureIfChanged(&tracker, auto_mode, value,
                                                 [&](int v) { return rec.Push(v); });
    };
    tick(/*auto_mode=*/false, 900);
    Check(!tick(/*auto_mode=*/false, 900), "the same value is not pushed again");
    Check(rec.count() == 1, "one push so far");
    Check(tick(/*auto_mode=*/false, 800), "a changed value is pushed");
    Check(rec.count() == 2 && rec.pushed[1] == 800, "the new value is what is pushed");
    Check(!tick(/*auto_mode=*/false, 800), "and then it is held again");
    Check(rec.count() == 2, "no extra push");
}

void CheckAutoModeNeverPushes() {
    exposure::ExposureApplyTracker tracker;
    Recorder rec;
    auto tick = [&](bool auto_mode, int value) {
        return exposure::ApplyExposureIfChanged(&tracker, auto_mode, value,
                                                 [&](int v) { return rec.Push(v); });
    };
    Check(!tick(/*auto_mode=*/true, 900), "auto mode does not push");
    Check(rec.count() == 0, "nothing was pushed");
    tick(/*auto_mode=*/false, 900);
    Check(!tick(/*auto_mode=*/true, 900), "auto mode does not push after a manual push");
    Check(rec.count() == 1, "still one push");
}

void CheckManualAfterAutoPushesAgain() {
    exposure::ExposureApplyTracker tracker;
    Recorder rec;
    auto tick = [&](bool auto_mode, int value) {
        return exposure::ApplyExposureIfChanged(&tracker, auto_mode, value,
                                                 [&](int v) { return rec.Push(v); });
    };
    tick(/*auto_mode=*/false, 900);
    tick(/*auto_mode=*/true, 900);   // the operator switches to auto
    tick(/*auto_mode=*/true, 900);
    Check(tick(/*auto_mode=*/false, 900), "manual mode after auto pushes the same value again");
    Check(rec.count() == 2, "the second push happened");
}

void CheckInvalidatePushesAgain() {
    exposure::ExposureApplyTracker tracker;
    Recorder rec;
    auto tick = [&](bool auto_mode, int value) {
        return exposure::ApplyExposureIfChanged(&tracker, auto_mode, value,
                                                 [&](int v) { return rec.Push(v); });
    };
    tick(/*auto_mode=*/false, 900);
    Check(!tick(/*auto_mode=*/false, 900), "the value is held until it is invalidated");
    tracker.Invalidate();  // a resume re-runs the SDK's scan-mode setup
    Check(tick(/*auto_mode=*/false, 900), "an invalidated value is pushed again");
    Check(rec.count() == 2, "the second push happened");
}

void CheckRefusedPushIsRetried() {
    exposure::ExposureApplyTracker tracker;
    Recorder rec;
    rec.accept = false;
    auto tick = [&](bool auto_mode, int value) {
        return exposure::ApplyExposureIfChanged(&tracker, auto_mode, value,
                                                 [&](int v) { return rec.Push(v); });
    };
    tick(/*auto_mode=*/false, 900);
    Check(rec.count() == 1, "the refused push was attempted");
    Check(!tracker.pushed(), "a refused push is not remembered");
    tick(/*auto_mode=*/false, 900);
    Check(rec.count() == 2, "a refused push is retried on the next tick");
}

}  // namespace

int main() {
    CheckFirstApplication();
    CheckSkipWhenUnchanged();
    CheckAutoModeNeverPushes();
    CheckManualAfterAutoPushesAgain();
    CheckInvalidatePushesAgain();
    CheckRefusedPushIsRetried();
    if (g_failures != 0) {
        std::fprintf(stderr, "[exposure_apply_test] %d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("[exposure_apply_test] PASS: the config is pushed on change only; auto mode never "
                "pushes; a resume invalidates what was pushed; a refused push is retried\n");
    return 0;
}
