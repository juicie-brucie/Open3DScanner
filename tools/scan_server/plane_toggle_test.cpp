// Hardware-free selftest for the runtime plane-removal command (row #158).
//
// It drives the same handler the WebSocket server calls
// (openscanner::control::HandleControlMessage) and the same controller the
// scan thread applies, with a mock vendor in place of the SDK: the three
// toggles' calls, the old -> new transition log, the rejected-call path and
// the no-hammering rule are all exercised without a device.
//
// The last check is end-to-end over a real WebSocket: the command travels
// over the wire to a real ws_broadcast_server and comes back in the status
// message, so the JSON shape the operator types is the shape the server
// parses.
//
// The vendor's verdict is a mock's verdict here. What the vendor actually
// does with the toggles is row #71's observation, on hardware.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "control_state.h"
#include "plane_toggle.h"

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#include "openscanner/ws_broadcast_server.h"

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    if (ok) {
        std::printf("ok   %s\n", what.c_str());
        return;
    }
    ++g_failures;
    std::printf("FAIL %s\n", what.c_str());
}

bool Contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

// The mock vendor: it records the calls it is asked to make and returns a
// scripted verdict.
struct MockSink : openscanner::plane::VendorSink {
    enum Which { kAuto, kTurntable, kManual };
    struct Record {
        Which which;
        bool enabled;
        float plane[4];
    };

    std::vector<Record> calls;
    bool ok = true;
    int status = 0;
    const char* name = "OB_SCAN_SUCCESS";

    openscanner::plane::VendorResult Result() const {
        openscanner::plane::VendorResult result;
        result.ok = ok;
        result.status = status;
        result.name = name;
        return result;
    }

    openscanner::plane::VendorResult SetAutoRemove(bool enabled) override {
        calls.push_back({kAuto, enabled, {0.0f, 0.0f, 0.0f, 0.0f}});
        return Result();
    }

    openscanner::plane::VendorResult SetTurntableRemove(bool enabled) override {
        calls.push_back({kTurntable, enabled, {0.0f, 0.0f, 0.0f, 0.0f}});
        return Result();
    }

    openscanner::plane::VendorResult SetManualRemove(bool enabled, const float plane[4]) override {
        calls.push_back({kManual, enabled, {plane[0], plane[1], plane[2], plane[3]}});
        return Result();
    }
};

// The transition log, captured instead of printed - and echoed in the same
// format the scan server writes, so the selftest's output shows the lines a
// hardware run would show.
struct LogCapture {
    std::vector<std::string> lines;
};

void CaptureLine(void* ctx, const char* line) {
    LogCapture* capture = static_cast<LogCapture*>(ctx);
    capture->lines.emplace_back(line);
    openscanner::plane::WritePlaneLogLine(ctx, line);
}

// The startup seed: the env surface the binary has always had. The runtime
// command is additive - it does not replace this.
void CheckStartupSeed(openscanner::control::ControlState* state, MockSink* sink, LogCapture* capture) {
    std::printf("\n-- the startup env seed (unchanged behaviour) --\n");
    setenv("OPENSCANNER_PLANE_REMOVE", "1", 1);
    setenv("OPENSCANNER_TURNTABLE_PLANE_REMOVE", "1", 1);
    setenv("OPENSCANNER_MANUAL_PLANE", "0,0,1,-0.33", 1);

    capture->lines.clear();
    sink->calls.clear();
    state->plane.SeedFromEnv();
    openscanner::plane::ApplyContext startup;  // tick < 0: not inside a tick
    const int calls = state->plane.ApplyPending(sink, startup);

    Check(calls == 3, "the three env-seeded toggles are applied");
    Check(sink->calls.size() == 3, "the mock vendor saw three calls");
    if (sink->calls.size() == 3) {
        Check(sink->calls[0].which == MockSink::kAuto && sink->calls[0].enabled, "the auto toggle is enabled");
        Check(sink->calls[1].which == MockSink::kTurntable && sink->calls[1].enabled,
              "the turntable toggle is enabled");
        Check(sink->calls[2].which == MockSink::kManual && sink->calls[2].enabled, "the manual toggle is enabled");
        Check(sink->calls[2].plane[0] == 0.0f && sink->calls[2].plane[1] == 0.0f && sink->calls[2].plane[2] == 1.0f &&
                  sink->calls[2].plane[3] == -0.33f,
              "the manual plane is the one the env named");
    }
    Check(capture->lines.size() == 3, "each transition is logged");
    if (capture->lines.size() == 3) {
        Check(capture->lines[0] ==
                  "obscan_scan_plane_remove(true) -> OB_SCAN_SUCCESS (0) accepted [auto 0->1 startup]",
              "the auto transition line names the call, the verdict and the old -> new state");
        Check(capture->lines[1] ==
                  "obscan_scan_turntable_plane_remove(true) -> OB_SCAN_SUCCESS (0) accepted [turntable 0->1 startup]",
              "the turntable transition line");
        Check(capture->lines[2] ==
                  "obscan_scan_config_manaul_remove_plane(a,b,c,d) -> OB_SCAN_SUCCESS (0) accepted [manual 0->1 "
                  "plane=0,0,1,-0.33 startup]",
              "the manual transition line carries the plane");
    }
}

// The runtime command: apply and revert, mid-session.
void CheckRuntimeCommand(openscanner::control::ControlState* state, MockSink* sink, LogCapture* capture) {
    std::printf("\n-- the runtime command: apply and revert --\n");
    openscanner::plane::ApplyContext tick_context;
    tick_context.tick = 42;
    tick_context.points = 1234;

    capture->lines.clear();
    sink->calls.clear();
    openscanner::control::HandleControlMessage(
        R"({"cmd":"set_plane_removal","auto":false,"manual":""})", state);
    const int calls = state->plane.ApplyPending(sink, tick_context);

    Check(calls == 2, "the command reverts the auto and the manual toggles");
    if (sink->calls.size() == 2) {
        Check(sink->calls[0].which == MockSink::kAuto && !sink->calls[0].enabled,
              "the auto toggle is turned off");
        Check(sink->calls[1].which == MockSink::kManual && !sink->calls[1].enabled,
              "the manual toggle is turned off");
    }
    Check(capture->lines.size() == 2, "each transition is logged");
    if (capture->lines.size() == 2) {
        Check(capture->lines[0] ==
                  "obscan_scan_plane_remove(false) -> OB_SCAN_SUCCESS (0) accepted [auto 1->0 tick=42 points=1234]",
              "the transition line carries the tick and the point count, so a point-count series can be aligned");
        Check(capture->lines[1] ==
                  "obscan_scan_config_manaul_remove_plane(off) -> OB_SCAN_SUCCESS (0) accepted [manual 1->0 tick=42 "
                  "points=1234]",
              "the revert is logged with the call that made it");
    }
    Check(!state->plane.Desired().auto_enabled && !state->plane.Desired().manual_enabled,
          "the desired state follows the command");

    // A new manual plane while the toggle is on is re-asserted.
    std::printf("\n-- a new manual plane --\n");
    capture->lines.clear();
    sink->calls.clear();
    openscanner::control::HandleControlMessage(
        R"({"cmd":"set_plane_removal","manual":"0,0,1,-0.5"})", state);
    const int plane_calls = state->plane.ApplyPending(sink, tick_context);
    Check(plane_calls == 1, "the manual plane change is one call");
    if (!sink->calls.empty()) {
        Check(sink->calls[0].which == MockSink::kManual && sink->calls[0].enabled &&
                  sink->calls[0].plane[3] == -0.5f,
              "the call carries the new plane");
    }
    if (!capture->lines.empty()) {
        Check(capture->lines[0] ==
                  "obscan_scan_config_manaul_remove_plane(a,b,c,d) -> OB_SCAN_SUCCESS (0) accepted [manual 0->1 "
                  "plane=0,0,1,-0.5 tick=42 points=1234]",
              "the transition line carries the plane");
    }
}

// A rejected vendor call: it is logged as rejected, the state is not updated
// and the call is not hammered every tick.
void CheckRejectedCall(openscanner::control::ControlState* state, MockSink* sink, LogCapture* capture) {
    std::printf("\n-- a rejected vendor call --\n");
    openscanner::plane::ApplyContext tick_context;
    tick_context.tick = 7;
    tick_context.points = 99;

    sink->ok = false;
    sink->status = -1;
    sink->name = "OB_SCAN_ERROR_PARAM";
    capture->lines.clear();
    sink->calls.clear();
    openscanner::control::HandleControlMessage(R"({"cmd":"set_plane_removal","auto":true})", state);
    const int calls = state->plane.ApplyPending(sink, tick_context);

    Check(calls == 1, "the rejected toggle is still attempted once");
    Check(capture->lines.size() == 1 && Contains(capture->lines[0], "rejected"),
          "the rejection is logged");
    Check(!state->plane.Applied().auto_enabled, "the applied state keeps the vendor's last verdict");
    Check(state->plane.Desired().auto_enabled, "the desired state keeps the operator's request");

    const int again = state->plane.ApplyPending(sink, tick_context);
    Check(again == 0, "the rejected call is not retried every tick");
    Check(capture->lines.size() == 1, "no second log line for the same request");

    // A new command retries it.
    capture->lines.clear();
    sink->calls.clear();
    sink->ok = true;
    sink->status = 0;
    sink->name = "OB_SCAN_SUCCESS";
    openscanner::control::HandleControlMessage(R"({"cmd":"set_plane_removal","auto":true})", state);
    const int retried = state->plane.ApplyPending(sink, tick_context);
    Check(retried == 1, "a new command retries the toggle");
    Check(state->plane.Applied().auto_enabled, "the accepted toggle becomes the applied state");
}

// A malformed or unrelated command changes nothing.
void CheckBadCommands(openscanner::control::ControlState* state, MockSink* sink, LogCapture* capture) {
    std::printf("\n-- malformed and unrelated commands --\n");
    openscanner::plane::ApplyContext tick_context;
    tick_context.tick = 9;

    const openscanner::plane::PlaneState before = state->plane.Desired();

    capture->lines.clear();
    sink->calls.clear();
    openscanner::control::HandleControlMessage(R"({"cmd":"set_plane_removal","manual":"nonsense"})", state);
    const int calls = state->plane.ApplyPending(sink, tick_context);
    Check(calls == 0, "a malformed manual plane makes no vendor call");
    Check(capture->lines.size() == 1 && Contains(capture->lines[0], "rejected"),
          "the malformed command is reported");
    Check(state->plane.Desired().manual_enabled == before.manual_enabled,
          "the malformed command changes nothing");

    capture->lines.clear();
    sink->calls.clear();
    openscanner::control::HandleControlMessage(R"({"cmd":"set_fov","value":"wide"})", state);
    const int other = state->plane.ApplyPending(sink, tick_context);
    Check(other == 0, "another command is left alone");

    capture->lines.clear();
    sink->calls.clear();
    openscanner::control::HandleControlMessage(R"({"cmd":"set_plane_removal"})", state);
    const int empty = state->plane.ApplyPending(sink, tick_context);
    Check(empty == 0, "a command with no plane field makes no vendor call");
    Check(capture->lines.size() == 1 && Contains(capture->lines[0], "nothing to do"),
          "the empty command is reported");
}

#if !defined(_WIN32)
// A minimal WebSocket client: the RFC 6455 handshake and masked text
// frames, enough to drive the server's command path over a real socket.
class WsClient {
public:
    ~WsClient() {
        if (fd_ >= 0) close(fd_);
    }

    bool Connect(uint16_t port) {
        fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ < 0) return false;
        struct timeval timeout;
        timeout.tv_sec = 5;
        timeout.tv_usec = 0;
        setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(fd_, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (connect(fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) return false;

        const char* request =
            "GET / HTTP/1.1\r\n"
            "Host: 127.0.0.1\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Key: MDEyMzQ1Njc4OWFiY2RlZg==\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            "\r\n";
        if (send(fd_, request, strlen(request), 0) != static_cast<ssize_t>(strlen(request))) return false;

        std::string response;
        char buffer[512];
        while (response.find("\r\n\r\n") == std::string::npos) {
            const ssize_t n = recv(fd_, buffer, sizeof(buffer), 0);
            if (n <= 0) return false;
            response.append(buffer, static_cast<size_t>(n));
        }
        return response.find("101") != std::string::npos;
    }

    bool SendText(const std::string& payload) {
        uint8_t header[14];
        size_t header_len = 0;
        header[header_len++] = 0x81;  // FIN + text
        const size_t len = payload.size();
        if (len < 126) {
            header[header_len++] = static_cast<uint8_t>(0x80 | len);
        } else {
            header[header_len++] = static_cast<uint8_t>(0x80 | 126);
            header[header_len++] = static_cast<uint8_t>((len >> 8) & 0xFF);
            header[header_len++] = static_cast<uint8_t>(len & 0xFF);
        }
        const uint8_t mask[4] = {0x12, 0x34, 0x56, 0x78};
        memcpy(header + header_len, mask, sizeof(mask));
        header_len += sizeof(mask);
        if (send(fd_, header, header_len, 0) != static_cast<ssize_t>(header_len)) return false;

        std::vector<uint8_t> masked(payload.begin(), payload.end());
        for (size_t i = 0; i < masked.size(); ++i) masked[i] ^= mask[i % 4];
        if (masked.empty()) return true;
        return send(fd_, masked.data(), masked.size(), 0) == static_cast<ssize_t>(masked.size());
    }

    // Reads frames until a text frame arrives (or the socket goes quiet).
    bool ReadText(std::string* out) {
        for (int attempt = 0; attempt < 32; ++attempt) {
            uint8_t head[2];
            if (RecvExact(head, sizeof(head)) != sizeof(head)) return false;
            const uint8_t opcode = head[0] & 0x0F;
            uint64_t len = head[1] & 0x7F;
            if (len == 126) {
                uint8_t ext[2];
                if (RecvExact(ext, sizeof(ext)) != sizeof(ext)) return false;
                len = (static_cast<uint64_t>(ext[0]) << 8) | ext[1];
            } else if (len == 127) {
                uint8_t ext[8];
                if (RecvExact(ext, sizeof(ext)) != sizeof(ext)) return false;
                len = 0;
                for (int i = 0; i < 8; ++i) len = (len << 8) | ext[i];
            }
            std::vector<uint8_t> payload(static_cast<size_t>(len));
            if (len > 0 && RecvExact(payload.data(), payload.size()) != payload.size()) return false;
            if (opcode == 0x1) {
                out->assign(payload.begin(), payload.end());
                return true;
            }
            if (opcode == 0x8) return false;  // close
        }
        return false;
    }

private:
    ssize_t RecvExact(uint8_t* out, size_t len) {
        size_t got = 0;
        while (got < len) {
            const ssize_t n = recv(fd_, out + got, len - got, 0);
            if (n <= 0) return static_cast<ssize_t>(got);
            got += static_cast<size_t>(n);
        }
        return static_cast<ssize_t>(got);
    }

    int fd_ = -1;
};
#endif  // !_WIN32

// The command over a real WebSocket: the shape the operator types is the
// shape the server parses.
void CheckWebSocketRoundTrip() {
    std::printf("\n-- the command over a real WebSocket --\n");
#if defined(_WIN32)
    std::printf("note: the WebSocket round trip is not built for this platform\n");
#else
    openscanner::control::ControlState state;
    LogCapture capture;
    state.plane.SetLogger(&CaptureLine, &capture);

    openscanner::WsBroadcastServer server;
    server.SetTextMessageHandler([&state, &server](const std::string& message) {
        openscanner::control::HandleControlMessage(message, &state);
        std::string status = "{\"type\":\"status\",\"planeRemoval\":";
        openscanner::plane::AppendPlaneStateJson(state.plane.Desired(), &status);
        status += "}";
        server.BroadcastText(status);
    });

    uint16_t port = 0;
    for (uint16_t attempt = 0; attempt < 12; ++attempt) {
        const uint16_t candidate = static_cast<uint16_t>(18720 + attempt);
        if (server.Start(candidate)) {
            port = candidate;
            break;
        }
    }
    Check(port != 0, "the WebSocket server starts");
    if (port == 0) return;

    WsClient client;
    Check(client.Connect(port), "the client connects");
    Check(client.SendText(R"({"cmd":"set_plane_removal","auto":true,"turntable":true,"manual":"0,0,1,-0.33"})"),
          "the command is sent");

    std::string status;
    const bool got = client.ReadText(&status);
    Check(got, "the status message comes back");
    if (got) {
        Check(Contains(status, "\"auto\":true"), "the status carries the auto toggle");
        Check(Contains(status, "\"turntable\":true"), "the status carries the turntable toggle");
        Check(Contains(status, "\"manual\":true"), "the status carries the manual toggle");
    }
    Check(state.plane.Desired().auto_enabled && state.plane.Desired().turntable_enabled &&
              state.plane.Desired().manual_enabled,
          "the command reached the handler over the wire");
    server.Stop();
#endif
}

}  // namespace

int main() {
    openscanner::control::ControlState state;
    LogCapture capture;
    state.plane.SetLogger(&CaptureLine, &capture);
    MockSink sink;

    CheckStartupSeed(&state, &sink, &capture);
    CheckRuntimeCommand(&state, &sink, &capture);
    CheckRejectedCall(&state, &sink, &capture);
    CheckBadCommands(&state, &sink, &capture);
    CheckWebSocketRoundTrip();

    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
