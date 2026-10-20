#pragma once
// Runtime plane-removal control (row #158).
//
// The three vendor toggles were startup-only (the env surface this binary
// has always had) and the WS command handler had no plane command, so a
// single-session A/B could not turn a toggle off mid-session. This header
// holds the state machine that turns a WS command into the three vendor
// calls, the old -> new transition log, and the apply step the scan thread
// runs.
//
// The toggles are the vendor's: what they mean, that they are gate-free and
// live-callable, and the manual-plane struct layout are in
// docs/recon/findings/plane-removal.md. Nothing here decides whether a point
// is removed - the vendor's own per-frame executor does that.
//
// Threading: the WS reader thread only sets the desired state (atomics); the
// scan thread applies it and makes every vendor call, so a toggle can never
// race the tick's own vendor calls. The apply path allocates nothing and
// blocks only on the vendor call itself.
//
// The command (row #158):
//
//   {"cmd":"set_plane_removal","auto":true,"turntable":false,"manual":"0,0,1,-0.33"}
//
// Every field is optional; a field that is present changes that toggle only.
// `manual` takes the cut plane as "a,b,c,d"; an empty string reverts the
// manual plane removal. A malformed field rejects the whole command.

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "control_json.h"

namespace openscanner {
namespace plane {

// The three toggles as the operator sees them.
struct PlaneState {
    bool auto_enabled = false;
    bool turntable_enabled = false;
    bool manual_enabled = false;
    float manual_a = 0.0f;
    float manual_b = 0.0f;
    float manual_c = 0.0f;
    float manual_d = 0.0f;
};

// The vendor's answer to one call: whether it accepted, its status code and
// the status name (the caller owns the name table).
struct VendorResult {
    bool ok = false;
    int status = 0;
    const char* name = "";
};

// The three vendor calls the controller makes. The live path binds them to
// the SDK; the selftest binds them to a recorder, so the whole path runs
// without a device.
class VendorSink {
public:
    virtual ~VendorSink() = default;
    virtual VendorResult SetAutoRemove(bool enabled) = 0;
    virtual VendorResult SetTurntableRemove(bool enabled) = 0;
    virtual VendorResult SetManualRemove(bool enabled, const float plane[4]) = 0;
};

// Where the controller's messages go: a plain function pointer plus a
// context, so the apply path never allocates.
using LogSink = void (*)(void* ctx, const char* line);

// The log line the scan server writes (row #158). The prefix matches the
// rest of the worker's log, so a transition sits next to the point-count
// lines it is meant to be aligned with.
inline void WritePlaneLogLine(void* ctx, const char* line) {
    (void)ctx;
    std::printf("[worker] plane_removal: %s\n", line);
    std::fflush(stdout);
}

// What each transition line carries: the tick it happened in and the point
// count of the last published cloud, so a run's point-count series can be
// aligned with the toggle moment. tick < 0 means "not inside a tick" (the
// startup apply).
struct ApplyContext {
    int tick = -1;
    uint32_t points = 0;
};

// "a,b,c,d" -> the four plane coefficients. False if the text is not four
// finite numbers.
inline bool ParseManualPlane(const std::string& text, float* a, float* b, float* c, float* d) {
    if (text.empty()) return false;
    float values[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    char extra = 0;
    // The trailing %c makes trailing junk a parse failure instead of a
    // silently ignored tail.
    if (std::sscanf(text.c_str(), "%f,%f,%f,%f %c", &values[0], &values[1], &values[2], &values[3], &extra) != 4) {
        return false;
    }
    for (int i = 0; i < 4; ++i) {
        if (!std::isfinite(values[i])) return false;
    }
    *a = values[0];
    *b = values[1];
    *c = values[2];
    *d = values[3];
    return true;
}

inline bool SamePlane(const PlaneState& a, const PlaneState& b) {
    return a.manual_a == b.manual_a && a.manual_b == b.manual_b && a.manual_c == b.manual_c &&
           a.manual_d == b.manual_d;
}

// The plane state as JSON, appended to `out` (no temporary string).
inline void AppendPlaneStateJson(const PlaneState& state, std::string* out) {
    out->append("{\"auto\":");
    out->append(state.auto_enabled ? "true" : "false");
    out->append(",\"turntable\":");
    out->append(state.turntable_enabled ? "true" : "false");
    out->append(",\"manual\":");
    out->append(state.manual_enabled ? "true" : "false");
    out->append("}");
}

// The state machine.
//
// `desired` is what the operator asked for; `applied` is what the vendor
// last accepted. A toggle is (re)issued when the two differ, which is also
// why a rejected call is not retried every tick: it is only tried again when
// a new command arrives (the generation changes).
class PlaneController {
public:
    // Where the controller's messages go: the transition lines and a
    // rejected command's reason. Set once, before the server starts - both
    // the scan thread and the WS reader thread read it.
    void SetLogger(LogSink log, void* ctx) {
        log_ = log;
        log_ctx_ = ctx;
    }

    // The startup toggles: the env surface this binary has always had. The
    // runtime command is additive - it does not replace this.
    void SeedFromEnv();

    // A WS command. Returns true when the message was a plane-removal
    // command (so the caller can tell it was ours), false otherwise. A
    // malformed field rejects the whole command: nothing changes and the
    // reason is logged.
    bool ApplyCommand(const std::string& json);

    // The scan thread: apply whatever changed since the last call. Returns
    // the number of vendor calls made.
    int ApplyPending(VendorSink* sink, const ApplyContext& ctx);

    // The scan thread: re-assert the enabled toggles. Whether the toggles
    // survive a pause is not stated in the findings, so the resume path
    // re-asserts them (this is what the startup env path always did).
    int ReassertEnabled(VendorSink* sink, const ApplyContext& ctx);

    PlaneState Desired() const;
    PlaneState Applied() const;

private:
    void LogTransition(const char* call, const char* arg, const VendorResult& result, const char* toggle, bool from,
                       bool to, const ApplyContext& ctx, const char* plane) const;

    std::atomic<bool> want_auto_{false};
    std::atomic<bool> want_turntable_{false};
    std::atomic<bool> want_manual_{false};
    std::atomic<float> want_a_{0.0f};
    std::atomic<float> want_b_{0.0f};
    std::atomic<float> want_c_{0.0f};
    std::atomic<float> want_d_{0.0f};

    std::atomic<bool> applied_auto_{false};
    std::atomic<bool> applied_turntable_{false};
    std::atomic<bool> applied_manual_{false};
    std::atomic<float> applied_a_{0.0f};
    std::atomic<float> applied_b_{0.0f};
    std::atomic<float> applied_c_{0.0f};
    std::atomic<float> applied_d_{0.0f};

    // Bumped by every accepted command; the apply step only runs when it
    // changed, so a rejected vendor call is not retried until the next
    // command.
    std::atomic<uint64_t> generation_{0};
    // The scan thread's own copy of the generation it last applied.
    uint64_t applied_generation_ = 0;

    LogSink log_ = nullptr;
    void* log_ctx_ = nullptr;
};

inline void PlaneController::SeedFromEnv() {
    const char* pr = std::getenv("OPENSCANNER_PLANE_REMOVE");
    if (pr != nullptr && std::string(pr) == "1") want_auto_.store(true);
    const char* tpr = std::getenv("OPENSCANNER_TURNTABLE_PLANE_REMOVE");
    if (tpr != nullptr && std::string(tpr) == "1") want_turntable_.store(true);
    const char* mp = std::getenv("OPENSCANNER_MANUAL_PLANE");
    if (mp != nullptr && mp[0] != '\0') {
        float a = 0.0f, b = 0.0f, c = 0.0f, d = 0.0f;
        if (ParseManualPlane(mp, &a, &b, &c, &d)) {
            want_a_.store(a);
            want_b_.store(b);
            want_c_.store(c);
            want_d_.store(d);
            want_manual_.store(true);
        } else {
            std::fprintf(stderr, "[plane_removal] OPENSCANNER_MANUAL_PLANE must be \"a,b,c,d\"\n");
        }
    }
    generation_.fetch_add(1, std::memory_order_release);
}

inline bool PlaneController::ApplyCommand(const std::string& json) {
    std::string cmd;
    if (!controljson::FindJsonValue(json, "cmd", &cmd)) return false;
    if (cmd != "set_plane_removal") return false;

    bool have_auto = false;
    bool auto_value = false;
    bool have_turntable = false;
    bool turntable_value = false;
    bool have_manual = false;
    bool manual_value = false;
    float manual_a = 0.0f, manual_b = 0.0f, manual_c = 0.0f, manual_d = 0.0f;

    // Every field is parsed before anything is stored, so a malformed field
    // rejects the whole command instead of applying half of it.
    std::string raw;
    if (controljson::FindJsonValue(json, "auto", &raw)) {
        if (raw == "true") {
            auto_value = true;
        } else if (raw == "false") {
            auto_value = false;
        } else {
            if (log_ != nullptr) log_(log_ctx_, "'auto' must be true or false - the command is rejected");
            return true;
        }
        have_auto = true;
    }
    if (controljson::FindJsonValue(json, "turntable", &raw)) {
        if (raw == "true") {
            turntable_value = true;
        } else if (raw == "false") {
            turntable_value = false;
        } else {
            if (log_ != nullptr) log_(log_ctx_, "'turntable' must be true or false - the command is rejected");
            return true;
        }
        have_turntable = true;
    }
    if (controljson::FindJsonValue(json, "manual", &raw)) {
        have_manual = true;
        if (raw.empty()) {
            manual_value = false;  // revert the manual plane removal
        } else if (ParseManualPlane(raw, &manual_a, &manual_b, &manual_c, &manual_d)) {
            manual_value = true;
        } else {
            if (log_ != nullptr) {
                log_(log_ctx_, "'manual' must be \"a,b,c,d\" or \"\" - the command is rejected");
            }
            return true;
        }
    }
    if (!have_auto && !have_turntable && !have_manual) {
        if (log_ != nullptr) {
            log_(log_ctx_, "the command carries no auto/turntable/manual field - nothing to do");
        }
        return true;
    }

    // The plane coefficients are stored before the flag that publishes them,
    // so a reader that sees the flag also sees the plane it belongs to.
    if (have_manual && manual_value) {
        want_a_.store(manual_a, std::memory_order_relaxed);
        want_b_.store(manual_b, std::memory_order_relaxed);
        want_c_.store(manual_c, std::memory_order_relaxed);
        want_d_.store(manual_d, std::memory_order_relaxed);
    }
    if (have_auto) want_auto_.store(auto_value, std::memory_order_release);
    if (have_turntable) want_turntable_.store(turntable_value, std::memory_order_release);
    if (have_manual) want_manual_.store(manual_value, std::memory_order_release);
    generation_.fetch_add(1, std::memory_order_release);
    return true;
}

inline PlaneState PlaneController::Desired() const {
    PlaneState state;
    state.auto_enabled = want_auto_.load(std::memory_order_acquire);
    state.turntable_enabled = want_turntable_.load(std::memory_order_acquire);
    // The manual flag is read before the plane it publishes (the writer
    // stores the plane first).
    state.manual_enabled = want_manual_.load(std::memory_order_acquire);
    state.manual_a = want_a_.load(std::memory_order_relaxed);
    state.manual_b = want_b_.load(std::memory_order_relaxed);
    state.manual_c = want_c_.load(std::memory_order_relaxed);
    state.manual_d = want_d_.load(std::memory_order_relaxed);
    return state;
}

inline PlaneState PlaneController::Applied() const {
    PlaneState state;
    state.auto_enabled = applied_auto_.load(std::memory_order_acquire);
    state.turntable_enabled = applied_turntable_.load(std::memory_order_acquire);
    state.manual_enabled = applied_manual_.load(std::memory_order_acquire);
    state.manual_a = applied_a_.load(std::memory_order_relaxed);
    state.manual_b = applied_b_.load(std::memory_order_relaxed);
    state.manual_c = applied_c_.load(std::memory_order_relaxed);
    state.manual_d = applied_d_.load(std::memory_order_relaxed);
    return state;
}

inline void PlaneController::LogTransition(const char* call, const char* arg, const VendorResult& result,
                                           const char* toggle, bool from, bool to, const ApplyContext& ctx,
                                           const char* plane) const {
    if (log_ == nullptr) return;
    char line[320];
    int n = std::snprintf(line, sizeof(line), "%s(%s) -> %s (%d) %s [%s %d->%d", call, arg, result.name, result.status,
                           result.ok ? "accepted" : "rejected", toggle, from ? 1 : 0, to ? 1 : 0);
    if (n < 0) return;
    if (n >= static_cast<int>(sizeof(line))) n = static_cast<int>(sizeof(line)) - 1;
    if (plane != nullptr) {
        int m = std::snprintf(line + n, sizeof(line) - static_cast<size_t>(n), " plane=%s", plane);
        if (m > 0) n += m;
        if (n >= static_cast<int>(sizeof(line))) n = static_cast<int>(sizeof(line)) - 1;
    }
    if (ctx.tick >= 0) {
        int m = std::snprintf(line + n, sizeof(line) - static_cast<size_t>(n), " tick=%d points=%u", ctx.tick,
                              ctx.points);
        if (m > 0) n += m;
    } else {
        int m = std::snprintf(line + n, sizeof(line) - static_cast<size_t>(n), " startup");
        if (m > 0) n += m;
    }
    if (n >= static_cast<int>(sizeof(line)) - 2) n = static_cast<int>(sizeof(line)) - 2;
    line[n++] = ']';
    line[n] = '\0';
    log_(log_ctx_, line);
}

inline int PlaneController::ApplyPending(VendorSink* sink, const ApplyContext& ctx) {
    if (sink == nullptr) return 0;
    const uint64_t generation = generation_.load(std::memory_order_acquire);
    if (generation == applied_generation_) return 0;
    const PlaneState want = Desired();
    // A command that landed while the state was being read: leave it for the
    // next tick rather than acting on a half-updated state.
    if (generation_.load(std::memory_order_acquire) != generation) return 0;

    const PlaneState have = Applied();
    int calls = 0;

    if (want.auto_enabled != have.auto_enabled) {
        const VendorResult result = sink->SetAutoRemove(want.auto_enabled);
        if (result.ok) applied_auto_.store(want.auto_enabled, std::memory_order_release);
        LogTransition("obscan_scan_plane_remove", want.auto_enabled ? "true" : "false", result, "auto",
                      have.auto_enabled, want.auto_enabled, ctx, nullptr);
        ++calls;
    }

    if (want.turntable_enabled != have.turntable_enabled) {
        const VendorResult result = sink->SetTurntableRemove(want.turntable_enabled);
        if (result.ok) applied_turntable_.store(want.turntable_enabled, std::memory_order_release);
        LogTransition("obscan_scan_turntable_plane_remove", want.turntable_enabled ? "true" : "false", result,
                      "turntable", have.turntable_enabled, want.turntable_enabled, ctx, nullptr);
        ++calls;
    }

    const bool manual_plane_changed = want.manual_enabled && !SamePlane(want, have);
    if (want.manual_enabled != have.manual_enabled || manual_plane_changed) {
        const float plane[4] = {want.manual_a, want.manual_b, want.manual_c, want.manual_d};
        const VendorResult result = sink->SetManualRemove(want.manual_enabled, plane);
        if (result.ok) {
            applied_manual_.store(want.manual_enabled, std::memory_order_release);
            applied_a_.store(want.manual_a, std::memory_order_relaxed);
            applied_b_.store(want.manual_b, std::memory_order_relaxed);
            applied_c_.store(want.manual_c, std::memory_order_relaxed);
            applied_d_.store(want.manual_d, std::memory_order_relaxed);
        }
        char plane_text[64] = {0};
        if (want.manual_enabled) {
            std::snprintf(plane_text, sizeof(plane_text), "%g,%g,%g,%g", want.manual_a, want.manual_b, want.manual_c,
                          want.manual_d);
        }
        LogTransition("obscan_scan_config_manaul_remove_plane", want.manual_enabled ? "a,b,c,d" : "off", result, "manual",
                      have.manual_enabled, want.manual_enabled, ctx, want.manual_enabled ? plane_text : nullptr);
        ++calls;
    }

    applied_generation_ = generation;
    return calls;
}

inline int PlaneController::ReassertEnabled(VendorSink* sink, const ApplyContext& ctx) {
    if (sink == nullptr) return 0;
    const PlaneState want = Desired();
    int calls = 0;

    if (want.auto_enabled) {
        const VendorResult result = sink->SetAutoRemove(true);
        if (result.ok) applied_auto_.store(true, std::memory_order_release);
        LogTransition("obscan_scan_plane_remove", "true", result, "auto", true, true, ctx, nullptr);
        ++calls;
    }
    if (want.turntable_enabled) {
        const VendorResult result = sink->SetTurntableRemove(true);
        if (result.ok) applied_turntable_.store(true, std::memory_order_release);
        LogTransition("obscan_scan_turntable_plane_remove", "true", result, "turntable", true, true, ctx, nullptr);
        ++calls;
    }
    if (want.manual_enabled) {
        const float plane[4] = {want.manual_a, want.manual_b, want.manual_c, want.manual_d};
        const VendorResult result = sink->SetManualRemove(true, plane);
        if (result.ok) {
            applied_manual_.store(true, std::memory_order_release);
            applied_a_.store(want.manual_a, std::memory_order_relaxed);
            applied_b_.store(want.manual_b, std::memory_order_relaxed);
            applied_c_.store(want.manual_c, std::memory_order_relaxed);
            applied_d_.store(want.manual_d, std::memory_order_relaxed);
        }
        char plane_text[64] = {0};
        std::snprintf(plane_text, sizeof(plane_text), "%g,%g,%g,%g", want.manual_a, want.manual_b, want.manual_c,
                      want.manual_d);
        LogTransition("obscan_scan_config_manaul_remove_plane", "a,b,c,d", result, "manual", true, true, ctx,
                      plane_text);
        ++calls;
    }
    return calls;
}

}  // namespace plane
}  // namespace openscanner
