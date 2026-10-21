#pragma once
// The request dispatcher behind the XPC listener (row #170).
//
// Translates one protocol Op into one claim-machine event and one Reply. Kept
// separate from the XPC transport so the whole request path — including the
// unknown-op refusal — is testable with no XPC and no device.

#include <memory>
#include <string>

#include "device_claim.h"
#include "helper_protocol.h"
#include "helper_state.h"

namespace openscanner::helper {

class HelperService {
public:
    HelperService(std::unique_ptr<DeviceClaim> claim, LogSink log);

    HelperService(const HelperService&) = delete;
    HelperService& operator=(const HelperService&) = delete;

    // The start event: claim on start. The daemon calls it once, after the
    // listener exists.
    Reply Start() { return machine_.Start(); }

    Reply Handle(Op op);

    // The quit path: release if held. The process exits afterwards; the XPC
    // listener is not torn down here.
    Reply Stop();

    ClaimState state() const { return machine_.state(); }
    const char* backend_name() const { return machine_.backend_name(); }
    LogSink log_sink() const { return log_; }

private:
    ClaimMachine machine_;
    LogSink log_;
};

// The transport's own unknown-name path: `name` comes off the wire and may be
// anything, so it maps to a refusal rather than a default.
Reply HandleOpName(HelperService& service, const std::string& name);

}  // namespace openscanner::helper
