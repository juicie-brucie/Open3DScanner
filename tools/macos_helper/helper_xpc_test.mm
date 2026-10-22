// Device-free XPC handshake test (row #170).
//
// A real XPC listener and a real connection, both in this process: the
// listener is anonymous and the client connects to its endpoint, so no
// launchd registration, no entitlements and no root are involved. The four
// protocol ops travel as XPC dictionaries and are answered by the same
// HelperService the daemon uses, over a fake device backend.
//
// Row #188: the listener's cancellation event is delivered asynchronously. A
// teardown that destroys the HelperService immediately after the Listener used
// to leave that event to run against freed memory (the intermittent SIGSEGV in
// _dispatch_mach_cancel_invoke -> Log -> log_sink()). TestTeardownRepeat
// exercises that order repeatedly.
//
// What this does not cover: launchd registration, code signing, and the
// device's own behaviour. Those are the operator gate.

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "device_claim.h"
#include "helper_service.h"
#include "helper_xpc.h"

namespace {

using openscanner::helper::ClaimState;
using openscanner::helper::Client;
using openscanner::helper::DeviceClaim;
using openscanner::helper::HelperService;
using openscanner::helper::Listener;
using openscanner::helper::Op;
using openscanner::helper::Reply;
using openscanner::helper::MakeAnonymousListener;

int g_checks = 0;
int g_failures = 0;

void Check(bool condition, const std::string& name) {
    ++g_checks;
    if (condition) {
        std::printf("  ok   %s\n", name.c_str());
        return;
    }
    ++g_failures;
    std::printf("  FAIL %s\n", name.c_str());
}

struct FakeClaim : DeviceClaim {
    FakeClaim(int* acquire_count, int* release_count) : acquires(acquire_count), releases(release_count) {}
    const char* Name() const override { return "fake"; }
    bool Acquire(std::string*) override {
        ++*acquires;
        return true;
    }
    void Release() override { ++*releases; }
    int* acquires;
    int* releases;
};

void TestHandshake() {
    std::printf("xpc handshake\n");
    int acquires = 0;
    int releases = 0;
    std::vector<std::string> log;
    HelperService service(std::make_unique<FakeClaim>(&acquires, &releases),
                          [&log](const std::string& line) { log.push_back(line); });

    std::string error;
    std::unique_ptr<Listener> listener = MakeAnonymousListener(&service, &error);
    Check(listener != nullptr, "an anonymous listener starts");
    if (!listener) {
        std::printf("  error: %s\n", error.c_str());
        return;
    }

    Client client = Client::ConnectEndpoint(listener->endpoint);
    Check(client.valid(), "the client connects to the listener's endpoint");
    if (!client.valid()) return;

    Reply reply = client.Send(Op::kPing, &error);
    Check(reply.ok && reply.detail == "pong", "ping crosses XPC and answers pong");
    Check(reply.state == ClaimState::kIdle, "ping reports idle before any claim");

    reply = client.Send(Op::kClaim, &error);
    Check(reply.ok && reply.state == ClaimState::kClaimed, "claim crosses XPC and reports claimed");
    Check(acquires == 1 && releases == 0, "the claim reached the backend");

    reply = client.Send(Op::kStatus, &error);
    Check(reply.ok && reply.state == ClaimState::kClaimed, "status reports the claim");
    Check(reply.detail.find("backend=fake") != std::string::npos, "status names the backend");

    reply = client.Send(Op::kRelease, &error);
    Check(reply.ok && reply.state == ClaimState::kReleased, "release crosses XPC and reports released");
    Check(releases == 1, "the release reached the backend exactly once");

    reply = client.Send(Op::kClaim, &error);
    Check(reply.ok && reply.state == ClaimState::kClaimed, "a second claim is served");
    Check(acquires == 2, "the second claim reached the backend");

    reply = client.SendNamed("nonsense", &error);
    Check(!reply.ok, "an unknown op is refused over the wire");
    Check(reply.detail == "unknown op: nonsense", "the refusal echoes the op name");

    const Reply stopped = service.Stop();
    Check(stopped.ok && stopped.state == ClaimState::kReleased, "Stop releases the held claim");
    Check(releases == 2, "Stop released exactly the one claim still held");

    reply = client.Send(Op::kStatus, &error);
    Check(reply.ok && reply.state == ClaimState::kReleased, "the listener still answers after Stop");
    Check(reply.detail.find("backend=fake") != std::string::npos, "the reply still names the backend");

    if (g_failures) {
        std::printf("  transition log:\n");
        for (const std::string& line : log) std::printf("    %s\n", line.c_str());
        std::printf("  last client error: '%s'\n", error.c_str());
    }
}

// Row #188: the exact order that crashed. Each scope ends the client, then
// the listener, then the service; the cancellation event is delivered on the
// listener queue after the listener is cancelled, so the handler must not
// touch the service once teardown has begun.
void TestTeardownRepeat() {
    std::printf("teardown repeat\n");
    int completed = 0;
    for (int i = 0; i < 50; ++i) {
        int acquires = 0;
        int releases = 0;
        std::vector<std::string> log;
        HelperService service(std::make_unique<FakeClaim>(&acquires, &releases),
                              [&log](const std::string& line) { log.push_back(line); });
        std::string error;
        std::unique_ptr<Listener> listener = MakeAnonymousListener(&service, &error);
        if (!listener) {
            Check(false, "a listener starts in the teardown loop");
            return;
        }
        Client client = Client::ConnectEndpoint(listener->endpoint);
        if (!client.valid()) {
            Check(false, "a client connects in the teardown loop");
            return;
        }
        const Reply ping = client.Send(Op::kPing, &error);
        if (!ping.ok) {
            Check(false, "ping crosses XPC in the teardown loop");
            return;
        }
        ++completed;
        // client -> listener -> service -> log, at the closing brace.
    }
    Check(completed == 50, "50 client/listener/service teardowns complete");
}

void TestUnconnectedClient() {
    std::printf("unconnected client\n");
    // No connection at all: the bounded send reports instead of attempting
    // anything. (Sending on a connection to an unregistered Mach service is
    // not tested here: on this OS that call faults inside libxpc/objc when the
    // connection is already invalid, which would make the suite flaky.)
    Client client;
    Check(!client.valid(), "a default client is not connected");

    std::string error;
    const Reply reply = client.Send(Op::kPing, &error);
    Check(!reply.ok, "a send without a connection is refused");
    Check(error == "not connected", "the refusal names the reason");

    Client unnamed = Client::ConnectMachService("");
    Check(!unnamed.valid(), "an empty Mach service name is refused");
}

}  // namespace

int main() {
    TestHandshake();
    TestTeardownRepeat();
    TestUnconnectedClient();
    std::printf("helper_xpc_test: %d check(s), %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
