// Row #153: when the exposure/laser config has to be pushed to the device.
//
// `obscan_scan_config_combined_exposure_and_laser_current` is a setter — it
// "pushes to the device" (docs/recon/findings/scan-live-data.md §18) — so the
// device holds the value until something else changes it. The vendor's own
// client applies it on change (its auto-exposure ramp pushes a new value when
// it computes one), not once per frame; the record for this row carries the
// measurement and the log evidence.
//
// This header holds the decision and the push, with the vendor call passed
// in, so the whole path can be tested without the SDK.
#pragma once

namespace openscanner::scan_server {

// Tracks what was last pushed to the device so the tick can skip the call
// when nothing changed.
//
// Single-threaded by construction: the tick owns it; the control state it
// compares against is atomic (the control message handler runs on another
// thread).
class ExposureApplyTracker {
public:
    // True when the value must be pushed now.
    //
    // The value is pushed when it differs from the last one pushed, and also
    // when the last push was not a manual one — an auto period in between may
    // have moved the device on, so the same number has to be pushed again.
    bool NeedsPush(int value) const {
        return !pushed_ || !pushed_manual_ || pushed_value_ != value;
    }

    // Record that `value` was pushed in manual mode.
    void NotePushed(int value) {
        pushed_ = true;
        pushed_manual_ = true;
        pushed_value_ = value;
    }

    // Record that the device went back to auto: nothing is held, so the next
    // manual value must be pushed even when it is the same number.
    void NoteAuto() { pushed_manual_ = false; }

    // The device may have reset the value on its own — a resume re-runs the
    // SDK's scan-mode setup, which writes the laser current itself — so the
    // next manual tick pushes the value again.
    void Invalidate() { pushed_ = false; }

    bool pushed() const { return pushed_; }
    int pushed_value() const { return pushed_value_; }

private:
    bool pushed_ = false;
    bool pushed_manual_ = false;
    int pushed_value_ = 0;
};

// Pushes the config when it changed; `push` performs the vendor call and
// returns whether the device accepted it.
//
// The value is remembered only when the push succeeded, so a refused call is
// retried on the next tick. In auto mode nothing is pushed and the tracker
// is told that nothing is held.
//
// The template keeps the vendor call out of this header: the live path passes
// the SDK call, the hardware-free test passes a recorder.
template <typename Push>
bool ApplyExposureIfChanged(ExposureApplyTracker* tracker, bool auto_mode, int value, Push&& push) {
    if (tracker == nullptr) return false;
    if (auto_mode) {
        tracker->NoteAuto();
        return false;
    }
    if (!tracker->NeedsPush(value)) return false;
    if (push(value)) {
        tracker->NotePushed(value);
    }
    return true;
}

}  // namespace openscanner::scan_server
