#pragma once
// The scan server's control state and the WS command handler.
//
// Moved out of main.cpp so the control-message path - the plane-removal
// command included (row #158) - can be exercised without a device: the
// selftest drives the same handler the WebSocket server calls.
//
// The handler never touches the vendor SDK: it records what the operator
// asked for, and the scan thread acts on it (the plane toggles are the
// vendor's calls, so they are made where every other vendor call is made -
// see docs/recon/findings/plane-removal.md).

#include <algorithm>
#include <atomic>
#include <string>

#include "control_json.h"
#include "plane_toggle.h"

namespace openscanner {
namespace control {

// Combined exposure/laser-current range per obscan_api.h
// (fn_obscan_scan_config_combined_exposure_and_laser_current); reused as
// the UI's manual exposure/brightness slider range for both modes so one
// value means the same thing everywhere.
constexpr int kExposureMin = 1;
constexpr int kExposureMax = 1000;
constexpr int kDefaultExposure = 900;  // Matches the value this file already drove live scans with.

// Shared between the WS reader thread(s) (writer, via HandleControlMessage)
// and whichever thread owns the scan loop (reader, via its tick) — hence
// all-atomic, no locking. Mirrors the synthetic/live scan lifecycle: a
// scan is idle, scanning, or paused; exposure is either auto or a held
// manual value; FOV is a two-position switch on the real scanner (no
// continuous zoom exists in the hardware API — see obscan_api.h's
// ObScanFov) applied at scan creation, so for live mode this field is
// informational only once scanning has started (see RunLiveServer).
struct ControlState {
    std::atomic<bool> scanning{false};
    std::atomic<bool> paused{false};
    std::atomic<bool> finish_requested{false};
    std::atomic<bool> auto_exposure{true};
    std::atomic<int> exposure_value{kDefaultExposure};
    std::atomic<bool> fov_wide{false};
    // Row #158: the plane-removal state. The desired state is written by
    // the WS reader thread; the scan thread applies it and logs each
    // transition.
    plane::PlaneController plane;
};

// Applies one control command to `state`. Safe to call from any thread;
// callers that also need the command to affect real hardware (RunLiveServer)
// check the same atomics from their own loop rather than acting here, so
// nothing in this function ever touches the vendor SDK directly.
inline void HandleControlMessage(const std::string& json, ControlState* state) {
    std::string cmd;
    if (!controljson::FindJsonValue(json, "cmd", &cmd)) return;
    if (cmd == "start") {
        state->scanning = true;
        state->paused = false;
    } else if (cmd == "pause") {
        state->paused = true;
    } else if (cmd == "finish") {
        state->scanning = false;
        state->paused = false;
        state->finish_requested = true;
    } else if (cmd == "set_exposure") {
        bool is_auto = false;
        if (controljson::FindJsonBool(json, "auto", &is_auto) && is_auto) {
            state->auto_exposure = true;
        } else {
            int value = kDefaultExposure;
            if (controljson::FindJsonInt(json, "value", &value)) {
                value = std::max(kExposureMin, std::min(kExposureMax, value));
                state->exposure_value = value;
            }
            state->auto_exposure = false;
        }
    } else if (cmd == "set_fov") {
        std::string value;
        if (controljson::FindJsonValue(json, "value", &value)) {
            state->fov_wide = (value == "wide");
        }
    } else if (cmd == "set_plane_removal") {
        // Row #158: the runtime plane-removal command. The controller
        // records the desired state here; the scan thread turns it into the
        // vendor calls and logs the transition.
        state->plane.ApplyCommand(json);
    }
}

}  // namespace control
}  // namespace openscanner
