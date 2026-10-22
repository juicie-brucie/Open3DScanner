#include "helper_xpc.h"

#if defined(__APPLE__)

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <utility>

#include "helper_service.h"

namespace openscanner::helper {
namespace {

std::string DescribeError(xpc_object_t error) { return DescribeXpcError(error); }

// Marks the listener queue so the destructor can tell whether it is already
// running on it (a dispatch_sync from the queue itself would deadlock).
const void* const kServiceQueueKey = &kServiceQueueKey;

// One serial queue for the listener and its peers, alive for the process. It
// is deliberately never released: cancelling the listener does not drain an
// invoke already in flight on this queue, and releasing it under that invoke
// faults inside libdispatch (observed as a SIGSEGV in
// _dispatch_set_priority_and_voucher_slow). The daemon has one listener and
// the test one process, so a process-lifetime queue costs nothing.
dispatch_queue_t ServiceQueue() {
    static dispatch_queue_t queue = [] {
        dispatch_queue_t q = dispatch_queue_create("org.openscanner.helper.xpc", DISPATCH_QUEUE_SERIAL);
        dispatch_queue_set_specific(q, kServiceQueueKey, const_cast<void*>(kServiceQueueKey), nullptr);
        return q;
    }();
    return queue;
}

void Log(const HelperService* service, const std::string& line) {
    if (!service) return;
    LogSink sink = service->log_sink();
    if (sink) sink(line);
}

void DispatchRequest(xpc_connection_t peer, xpc_object_t message, HelperService* service) {
    const char* op_name = xpc_dictionary_get_string(message, "op");
    Reply reply;
    if (!op_name) {
        Log(service, "xpc: request without an 'op' key refused");
        reply = {false, service->state(), "missing 'op'"};
    } else {
        reply = HandleOpName(*service, op_name);
    }

    // A reply must be built from the request: a fresh dictionary would arrive
    // as a new message on the client's event handler and leave its pending
    // reply waiting until the connection is interrupted.
    xpc_object_t out = xpc_dictionary_create_reply(message);
    if (!out) {
        Log(service, "xpc: message carried no reply context; refusing it");
        return;
    }
    xpc_dictionary_set_bool(out, "ok", reply.ok);
    xpc_dictionary_set_string(out, "state", ClaimStateName(reply.state));
    xpc_dictionary_set_string(out, "detail", reply.detail.c_str());
    xpc_connection_send_message(peer, out);
    xpc_release(out);
}

// The handler owns one retain of the peer (taken here) and releases it on the
// terminal event. The HandlerState is captured by value: a block copy keeps it
// alive even if the Listener is gone, and Get() returns null after teardown,
// so a late event never touches a destroyed HelperService.
void InstallPeerHandler(xpc_connection_t peer, std::shared_ptr<HandlerState> state) {
    xpc_retain(peer);
    xpc_connection_set_event_handler(peer, ^(xpc_object_t event) {
        const xpc_type_t type = xpc_get_type(event);
        if (type == XPC_TYPE_ERROR) {
            HelperService* service = state->Get();
            if (service) Log(service, std::string("xpc: peer error: ") + DescribeError(event));
            // The handler owns the retain taken above. Release only on the
            // terminal event: an interrupted connection can still deliver a
            // later invalid event, and two releases would be one too many.
            if (event == XPC_ERROR_CONNECTION_INVALID) xpc_release(peer);
            return;
        }
        HelperService* service = state->Get();
        if (!service) return;  // torn down; the peer is being cancelled
        if (type != XPC_TYPE_DICTIONARY) {
            Log(service, "xpc: non-dictionary message ignored");
            return;
        }
        DispatchRequest(peer, event, service);
    });
    xpc_connection_resume(peer);
}

void InstallListenerHandler(Listener* listener) {
    std::shared_ptr<HandlerState> state = listener->state;
    dispatch_queue_t queue = listener->queue;
    xpc_connection_set_event_handler(listener->connection, ^(xpc_object_t event) {
        HelperService* service = state->Get();
        if (!service) return;  // torn down; the listener is being cancelled
        const xpc_type_t type = xpc_get_type(event);
        if (type == XPC_TYPE_ERROR) {
            Log(service, std::string("xpc: listener error: ") + DescribeError(event) +
                              " (a Mach service not registered with launchd reports this; "
                              "the claim and its release still run)");
            return;
        }
        if (type != XPC_TYPE_CONNECTION) return;
        xpc_connection_set_target_queue((xpc_connection_t)event, queue);
        InstallPeerHandler((xpc_connection_t)event, state);
    });
}

std::unique_ptr<Listener> MakeListener(xpc_connection_t connection, dispatch_queue_t queue,
                                       HelperService* service) {
    auto listener = std::make_unique<Listener>();
    listener->connection = connection;
    listener->queue = queue;
    listener->state = std::make_shared<HandlerState>();
    listener->state->service = service;
    InstallListenerHandler(listener.get());
    xpc_connection_resume(connection);
    return listener;
}

}  // namespace

std::string DescribeXpcError(xpc_object_t error) {
    if (xpc_get_type(error) != XPC_TYPE_ERROR) return "unknown XPC error";
    if (error == XPC_ERROR_CONNECTION_INVALID) {
        // The exact #179 failure: the name has no listener because no launchd
        // job registered it. Name the fix rather than the XPC constant.
        return "no Mach service is registered under that name in this session "
               "(install/start the openscanner-helper user LaunchAgent: "
               "tools/macos_helper/install_user_agent.sh install)";
    }
    if (error == XPC_ERROR_CONNECTION_INTERRUPTED) return "the helper connection was interrupted";
    const char* description = xpc_dictionary_get_string(error, XPC_ERROR_KEY_DESCRIPTION);
    if (description) return description;
    return "unknown XPC error";
}

Listener::~Listener() {
    // 1. Make every handler inert. A cancellation event is delivered
    //    asynchronously; without this, it can run after the caller has
    //    destroyed the HelperService and dereference it through log_sink()
    //    (row #188's intermittent SIGSEGV, in _dispatch_mach_cancel_invoke ->
    //    Log -> log_sink()).
    if (state) state->Stop();
    // 2. Cancel the listener and wait for the handlers already queued on the
    //    serial queue to run. Skip the wait when the destructor is itself
    //    running on that queue (a sync there would deadlock).
    if (connection) xpc_connection_cancel(connection);
    const bool on_queue = dispatch_get_specific(kServiceQueueKey) != nullptr;
    if (queue && !on_queue) dispatch_sync(queue, ^{});
    // 3. Release our references. The connection must not go before the queued
    //    cancellation invoke has run, which is what step 2 waits for.
    if (endpoint) {
        xpc_release(endpoint);
        endpoint = nullptr;
    }
    if (connection) {
        xpc_release(connection);
        connection = nullptr;
    }
    // No dispatch_release(queue): see ServiceQueue().
}

std::unique_ptr<Listener> MakeMachServiceListener(const char* mach_service_name, HelperService* service,
                                                  std::string* err) {
    if (!mach_service_name || !*mach_service_name) {
        if (err) *err = "empty Mach service name";
        return nullptr;
    }
    dispatch_queue_t queue = ServiceQueue();
    xpc_connection_t connection = xpc_connection_create_mach_service(
        mach_service_name, queue, XPC_CONNECTION_MACH_SERVICE_LISTENER);
    if (!connection) {
        // No dispatch_release(queue): ServiceQueue() is process-lifetime and
        // deliberately never released (see above).
        if (err) *err = std::string("could not create a listener for ") + mach_service_name;
        return nullptr;
    }
    Log(service, std::string("xpc: listening on Mach service ") + mach_service_name);
    return MakeListener(connection, queue, service);
}

std::unique_ptr<Listener> MakeAnonymousListener(HelperService* service, std::string* err) {
    dispatch_queue_t queue = ServiceQueue();
    xpc_connection_t connection = xpc_connection_create(nullptr, queue);
    if (!connection) {
        if (err) *err = "could not create an anonymous listener";
        return nullptr;
    }
    std::unique_ptr<Listener> listener = MakeListener(connection, queue, service);
    listener->endpoint = xpc_endpoint_create(connection);
    return listener;
}

Client::~Client() {
    if (connection_) {
        xpc_connection_cancel(connection_);
        xpc_release(connection_);
    }
}

Client::Client(Client&& other) noexcept : connection_(other.connection_) { other.connection_ = nullptr; }

Client& Client::operator=(Client&& other) noexcept {
    if (this != &other) {
        if (connection_) {
            xpc_connection_cancel(connection_);
            xpc_release(connection_);
        }
        connection_ = other.connection_;
        other.connection_ = nullptr;
    }
    return *this;
}

Client Client::ConnectMachService(const char* mach_service_name) {
    if (!mach_service_name || !*mach_service_name) return Client();
    // A client owns no queue: replies arrive on the queue passed to
    // send_message_with_reply, and the connection's event handler only sees
    // errors, which need no ordering.
    xpc_connection_t connection = xpc_connection_create_mach_service(
        mach_service_name, dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0), 0);
    if (!connection) return Client();
    xpc_connection_set_event_handler(connection, ^(xpc_object_t event) {
        (void)event;  // Errors surface on the reply path as well.
    });
    xpc_connection_resume(connection);
    return Client(connection);
}

Client Client::ConnectEndpoint(xpc_endpoint_t endpoint) {
    if (!endpoint) return Client();
    xpc_connection_t connection = xpc_connection_create_from_endpoint(endpoint);
    if (!connection) return Client();
    xpc_connection_set_target_queue(connection, dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0));
    xpc_connection_set_event_handler(connection, ^(xpc_object_t event) {
        (void)event;
    });
    xpc_connection_resume(connection);
    return Client(connection);
}

namespace {

struct Pending {
    std::mutex mutex;
    std::condition_variable done;
    bool finished = false;
    Reply reply;
    std::string error;
};

}  // namespace

Reply Client::Send(Op op, std::string* err) { return SendNamed(OpName(op), err); }

Reply Client::SendNamed(const std::string& op_name, std::string* err) {
    if (!connection_) {
        if (err) *err = "not connected";
        return {};
    }

    xpc_object_t message = xpc_dictionary_create(nullptr, nullptr, 0);
    xpc_dictionary_set_string(message, "op", op_name.c_str());

    auto pending = std::make_shared<Pending>();
    xpc_connection_send_message_with_reply(
        connection_, message, dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0), ^(xpc_object_t reply) {
            std::lock_guard<std::mutex> lock(pending->mutex);
            if (xpc_get_type(reply) == XPC_TYPE_DICTIONARY) {
                pending->reply.ok = xpc_dictionary_get_bool(reply, "ok");
                const char* state = xpc_dictionary_get_string(reply, "state");
                if (state) ClaimStateFromName(state, &pending->reply.state);
                const char* detail = xpc_dictionary_get_string(reply, "detail");
                if (detail) pending->reply.detail = detail;
            } else {
                pending->error = "reply was not a dictionary: " + DescribeError(reply);
            }
            pending->finished = true;
            pending->done.notify_all();
        });
    xpc_release(message);

    std::unique_lock<std::mutex> lock(pending->mutex);
    if (!pending->done.wait_for(lock, std::chrono::seconds(5), [&] { return pending->finished; })) {
        if (err) *err = "timed out waiting for the helper";
        return {};
    }
    if (!pending->error.empty()) {
        if (err) *err = pending->error;
        return {};
    }
    return pending->reply;
}

}  // namespace openscanner::helper

#endif  // __APPLE__
