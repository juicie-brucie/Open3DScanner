// Device-free test for the shim's claim query over a real XPC round trip
// (row #181).
//
// The fake helper is the real HelperService behind an anonymous listener; the
// channel under test is the real shim HelperClaimChannel, injected with that
// listener's endpoint. No launchd registration, no entitlement, no device and
// no root are involved. The product path (a Mach service name registered by
// the user LaunchAgent) is the same client code with a different connection;
// the unregistered-name text is asserted without opening a doomed connection.

#include <cstdio>
#include <memory>
#include <string>
#include <utility>

#include "device_claim.h"
#include "helper_claim.h"
#include "helper_service.h"
#include "helper_xpc.h"

namespace {

using openscanner::helper::ClaimState;
using openscanner::helper::Client;
using openscanner::helper::DeviceClaim;
using openscanner::helper::HelperService;
using openscanner::helper::Listener;
using openscanner::helper::MakeAnonymousListener;
using openscanner::helper::Op;
using openscanner::helper::Reply;

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
    const char* Name() const override { return "fake"; }
    bool Acquire(std::string*) override { return true; }
    void Release() override {}
};

void TestClaimQuery() {
    std::printf("claim query over a fake helper\n");

    // The product channel names the user LaunchAgent's Mach service.
    shim::HelperClaimChannel product;
    Check(product.service() == "org.openscanner.helper",
          "the product channel asks org.openscanner.helper");

    HelperService service(std::make_unique<FakeClaim>(), nullptr);
    std::string error;
    std::unique_ptr<Listener> listener = MakeAnonymousListener(&service, &error);
    Check(listener != nullptr, "the fake helper's listener starts");
    if (!listener) {
        std::printf("  error: %s\n", error.c_str());
        return;
    }

    auto client = std::make_unique<Client>(Client::ConnectEndpoint(listener->endpoint));
    Check(client->valid(), "the test client connects to the fake helper's endpoint");
    if (!client->valid()) return;

    // The real shim channel, over the real helper client.
    shim::HelperClaimChannel channel(std::move(client), "org.openscanner.helper.test");

    std::string detail;
    shim::ClaimChannel::State state = channel.Query(&detail);
    Check(state == shim::ClaimChannel::State::kNotClaimed && detail.find("idle") != std::string::npos,
          "status before any claim reports idle -> not-claimed");

    Reply reply = service.Start();
    Check(reply.ok && reply.state == ClaimState::kClaimed, "the fake helper holds the claim");
    detail.clear();
    state = channel.Query(&detail);
    Check(state == shim::ClaimChannel::State::kClaimedByHelper,
          "a claimed helper maps to claimed-by-helper");

    reply = service.Handle(Op::kRelease);
    Check(reply.ok && reply.state == ClaimState::kReleased, "release is served over XPC");
    detail.clear();
    state = channel.Query(&detail);
    Check(state == shim::ClaimChannel::State::kNotClaimed && detail.find("released") != std::string::npos,
          "status after release maps to not-claimed and names the state");

    reply = service.Handle(Op::kClaim);
    Check(reply.ok && reply.state == ClaimState::kClaimed, "claim is served over XPC");
    detail.clear();
    state = channel.Query(&detail);
    Check(state == shim::ClaimChannel::State::kClaimedByHelper,
          "status after an XPC claim reports claimed");

    reply = service.Handle(Op::kStatus);
    Check(reply.ok && reply.state == ClaimState::kClaimed && reply.detail.find("backend=fake") != std::string::npos,
          "status is served over XPC and names the backend");

    service.Stop();
    detail.clear();
    state = channel.Query(&detail);
    Check(state == shim::ClaimChannel::State::kNotClaimed,
          "status after the helper stops the claim reports not-claimed");
}

// The SDK's error globals are const pointers; DescribeXpcError takes the
// xpc_object_t the API uses. The cast is only for naming an error, never for
// sending one.
xpc_object_t ErrorObject(const void* error) { return const_cast<void*>(error); }

void TestUnregisteredServiceText() {
    std::printf("unregistered Mach service\n");
    // The #179 failure: the name has no launchd job. The text names the fix;
    // no connection is opened for this check.
    const std::string invalid =
        openscanner::helper::DescribeXpcError(ErrorObject(XPC_ERROR_CONNECTION_INVALID));
    Check(invalid.find("LaunchAgent") != std::string::npos,
          "an unregistered Mach service names the user LaunchAgent fix");
    Check(openscanner::helper::DescribeXpcError(ErrorObject(XPC_ERROR_CONNECTION_INTERRUPTED)) ==
              "the helper connection was interrupted",
          "an interrupted connection has its own text");
}

}  // namespace

int main() {
    TestClaimQuery();
    TestUnregisteredServiceText();
    std::printf("helper_claim_test: %d check(s), %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
