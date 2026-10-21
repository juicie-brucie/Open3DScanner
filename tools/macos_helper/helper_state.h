#pragma once
// The claim/release state machine (row #170).
//
// Events and what they guarantee:
//   Start()    claim on helper start. A failed claim leaves the machine
//              released and reports why; it never reports a claim it does not
//              hold.
//   Acquire()  an explicit claim (XPC `claim`). Idempotent while claimed.
//   Release()  drop the claim and stay alive (XPC `release`).
//   Unplug()   the device disappeared underneath a held claim. Releases once.
//   Shutdown() quit: release if held, then refuse further claims.
//
// The invariant the tests pin: the backend's Release() runs exactly once for
// every successful Acquire(), and not at all for a claim that was never
// taken. Every transition is logged in the form
//   claim: <from> -> <to> (<reason>)
// so a run's log shows the claim's whole life.
//
// Threading: the machine is mutex-protected because the XPC dispatch queue
// and the IOKit notification run loop can both call in. The log sink is
// called under that mutex; a sink must not call back into the machine.
//
// The spec is docs/recomp/FULL-INDEPENDENCE.md §5 L3 and §6 Phase D; this
// file implements no data path of any kind.

#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include "device_claim.h"
#include "helper_protocol.h"

namespace openscanner::helper {

using LogSink = std::function<void(const std::string& line)>;

class ClaimMachine {
public:
    ClaimMachine(std::unique_ptr<DeviceClaim> claim, LogSink log);
    ~ClaimMachine();

    ClaimMachine(const ClaimMachine&) = delete;
    ClaimMachine& operator=(const ClaimMachine&) = delete;

    Reply Start();
    Reply Acquire();
    Reply Release();
    Reply Unplug();
    Reply Shutdown();

    ClaimState state() const;
    const char* backend_name() const;

private:
    Reply AcquireLocked(const char* reason);
    Reply ReleaseLocked(const char* reason);
    void TransitionLocked(ClaimState to, const std::string& reason);
    void LogLocked(const std::string& line);

    std::unique_ptr<DeviceClaim> claim_;
    LogSink log_;
    // Recursive so a backend that reports the unplug synchronously from
    // inside Release() cannot deadlock the machine; `held_` is cleared before
    // that call, so the re-entrant unplug is a no-op rather than a second
    // release.
    mutable std::recursive_mutex mutex_;
    ClaimState state_ = ClaimState::kIdle;
    bool started_ = false;
    bool held_ = false;
    bool shutting_down_ = false;
};

}  // namespace openscanner::helper
