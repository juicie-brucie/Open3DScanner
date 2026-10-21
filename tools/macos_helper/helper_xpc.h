#pragma once
// The XPC transport for the helper (row #170): a programmatic listener on the
// helper side and a small blocking client for the app side. The service is
// registered with launchd by the plist template in launchd/; nothing here
// installs or bootstraps anything.
//
// The listener can also be created anonymous (no launchd, no Mach service)
// and handed its endpoint directly. That is the seam the device-free XPC test
// uses: a real listener, a real connection, a real round trip, in one process.
//
// Spec for the architecture: docs/recomp/FULL-INDEPENDENCE.md §5 L3, §6
// Phase D. This file carries no device data path.

#if defined(__APPLE__)

#include <xpc/xpc.h>

#include <memory>
#include <string>

#include "helper_protocol.h"

namespace openscanner::helper {

class HelperService;

// A resumed listener. Destroying it cancels the listener and drops the queue.
struct Listener {
    xpc_connection_t connection = nullptr;
    xpc_endpoint_t endpoint = nullptr;  // non-null only when anonymous
    dispatch_queue_t queue = nullptr;
    ~Listener();
};

// `mach_service_name` must match the plist's MachServices key. Registration
// errors (name not registered with launchd) arrive asynchronously on the
// listener and are logged through the service's sink, not returned here.
std::unique_ptr<Listener> MakeMachServiceListener(const char* mach_service_name, HelperService* service,
                                                  std::string* err);

// No launchd. The returned Listener owns the endpoint a client connects to.
std::unique_ptr<Listener> MakeAnonymousListener(HelperService* service, std::string* err);

// The human text for an XPC transport error (row #181). Exported so the
// unregistered-Mach-service case can be asserted without opening a doomed
// connection; the helper and the shim both log through it.
std::string DescribeXpcError(xpc_object_t error);

// The app side. Not called by the product in this row; ConnectMachService is
// what a later row wires into the app.
class Client {
public:
    Client() = default;
    ~Client();
    Client(Client&& other) noexcept;
    Client& operator=(Client&& other) noexcept;
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    static Client ConnectMachService(const char* mach_service_name);
    static Client ConnectEndpoint(xpc_endpoint_t endpoint);

    bool valid() const { return connection_ != nullptr; }

    // Blocking, with a 5 s bound so a hung helper cannot hang the caller.
    Reply Send(Op op, std::string* err);
    Reply SendNamed(const std::string& op_name, std::string* err);

private:
    explicit Client(xpc_connection_t connection) : connection_(connection) {}
    xpc_connection_t connection_ = nullptr;
};

}  // namespace openscanner::helper

#endif  // __APPLE__
