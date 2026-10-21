#include "helper_service.h"

#include <utility>

namespace openscanner::helper {

HelperService::HelperService(std::unique_ptr<DeviceClaim> claim, LogSink log)
    : machine_(std::move(claim), log), log_(std::move(log)) {}

Reply HelperService::Handle(Op op) {
    switch (op) {
        case Op::kPing:
            // A liveness answer that also tells the caller where the claim
            // stands: one round trip, no second request.
            return {true, machine_.state(), "pong"};
        case Op::kStatus:
            return {true, machine_.state(), std::string("backend=") + machine_.backend_name()};
        case Op::kClaim:
            return machine_.Acquire();
        case Op::kRelease:
            return machine_.Release();
    }
    return {false, machine_.state(), "unknown op"};
}

Reply HelperService::Stop() { return machine_.Shutdown(); }

Reply HandleOpName(HelperService& service, const std::string& name) {
    Op op;
    if (!OpFromName(name, &op)) {
        if (service.log_sink()) {
            service.log_sink()("xpc: refused unknown op '" + name + "'");
        }
        return {false, service.state(), "unknown op: " + name};
    }
    return service.Handle(op);
}

}  // namespace openscanner::helper
