#include "helper_state.h"

#include <utility>

namespace openscanner::helper {

ClaimMachine::ClaimMachine(std::unique_ptr<DeviceClaim> claim, LogSink log)
    : claim_(std::move(claim)), log_(std::move(log)) {
    // The backend reports a vanished device through this handler; the machine
    // turns it into the unplug event. Set before any claim exists so a device
    // that disappears between Acquire() and the handler's registration is
    // still reported.
    if (claim_) {
        claim_->SetUnplugHandler([this] { Unplug(); });
    }
}

ClaimMachine::~ClaimMachine() {
    // Best effort: a helper that is torn down without Shutdown() still drops
    // the claim rather than leaving the device held for the process's lifetime.
    Reply ignored = Shutdown();
    (void)ignored;
}

Reply ClaimMachine::Start() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    LogLocked("claim: start");
    if (started_) {
        LogLocked("claim: already started (no second claim)");
        return {held_, state_, "already started"};
    }
    started_ = true;
    return AcquireLocked("start");
}

Reply ClaimMachine::Acquire() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (shutting_down_) {
        LogLocked("claim: refusing claim while shutting down");
        return {false, state_, "helper is shutting down"};
    }
    return AcquireLocked("claim");
}

Reply ClaimMachine::Release() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return ReleaseLocked("release");
}

Reply ClaimMachine::Unplug() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!held_) {
        LogLocked("claim: unplug with no claim held (nothing to release)");
        return {true, state_, "no claim held"};
    }
    return ReleaseLocked("unplug");
}

Reply ClaimMachine::Shutdown() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (shutting_down_) {
        return {true, state_, "already shut down"};
    }
    LogLocked("claim: quit");
    Reply reply = ReleaseLocked("quit");
    shutting_down_ = true;
    return reply;
}

ClaimState ClaimMachine::state() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return state_;
}

const char* ClaimMachine::backend_name() const { return claim_ ? claim_->Name() : "none"; }

Reply ClaimMachine::AcquireLocked(const char* reason) {
    if (held_ || state_ == ClaimState::kClaimed) {
        LogLocked("claim: already claimed (no second backend call)");
        return {true, state_, "already claimed"};
    }
    TransitionLocked(ClaimState::kClaiming, reason);
    std::string why;
    const bool ok = claim_ && claim_->Acquire(&why);
    if (!ok) {
        if (why.empty()) why = "backend refused the claim";
        TransitionLocked(ClaimState::kReleased, "claim failed: " + why);
        return {false, state_, why};
    }
    held_ = true;
    TransitionLocked(ClaimState::kClaimed, std::string("device=") + claim_->Name());
    return {true, state_, std::string("claimed via ") + claim_->Name()};
}

Reply ClaimMachine::ReleaseLocked(const char* reason) {
    if (!held_) {
        if (state_ != ClaimState::kReleased) {
            TransitionLocked(ClaimState::kReleased, std::string(reason) + " with no claim");
        }
        LogLocked("claim: released (no claim held, backend not called)");
        return {true, state_, "no claim held"};
    }
    TransitionLocked(ClaimState::kReleasing, reason);
    // Dropped logically before the backend call: a backend that signals the
    // unplug from inside Release() then re-enters Unplug() and finds nothing
    // to do, instead of releasing twice.
    held_ = false;
    claim_->Release();
    TransitionLocked(ClaimState::kReleased, std::string(reason) + ": release complete");
    return {true, state_, std::string("released on ") + reason};
}

void ClaimMachine::TransitionLocked(ClaimState to, const std::string& reason) {
    const std::string line = std::string("claim: ") + ClaimStateName(state_) + " -> " +
                             ClaimStateName(to) + " (" + reason + ")";
    state_ = to;
    LogLocked(line);
}

void ClaimMachine::LogLocked(const std::string& line) {
    if (log_) log_(line);
}

}  // namespace openscanner::helper
