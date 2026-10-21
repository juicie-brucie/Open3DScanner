// Device-free test for the RPC shim (row #172).
//
// Two halves:
//   - the unit half pins the 11-element array layout, the no-data form, the
//     discovery command, the vendor framing and the enumeration array;
//   - the integration half writes a synthetic capture bundle (with the
//     recorder's own writer), serves it through the shim on the IR port, and
//     runs the real `raptor_client --live` against it: the client's own parse,
//     its data/empty tally and its saved frame are the verdict, so the wire
//     shape is checked by the independent client implementation, not by the
//     shim's.
//
// No device, no vendor daemon, no root. The A/B against the vendor daemon on
// the same scene is the hardware card.

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "../raptor_client/capture.h"
#include "openscanner/daemon_switch.h"
#include "raw_capture.h"
#include "bundle_source.h"
#include "discovery.h"
#include "frame_source.h"
#include "live_source.h"
#include "ob_words.h"
#include "msgpack.h"
#include "properties.h"
#include "shim_server.h"

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(const std::string& name, bool pass) {
    ++g_checks;
    std::printf("  %-64s %s\n", name.c_str(), pass ? "ok" : "FAIL");
    if (!pass) ++g_failures;
}

shim::Bytes Envelope(uint64_t msgid, const shim::Bytes& result) {
    shim::Packer p;
    p.array(4);
    p.uint(1);
    p.uint(msgid);
    p.nil();
    p.raw(result);
    return p.out;
}

std::string ReadFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// The count after `key` in a line such as "get_frame=12 data=9 empty=0".
long CountAfter(const std::string& text, const std::string& key) {
    const size_t at = text.find(key);
    if (at == std::string::npos) return -1;
    return std::strtol(text.c_str() + at + key.size(), nullptr, 10);
}

void TestFrameArray() {
    std::printf("frame array\n");
    shim::FramePayload frame;
    frame.e1_us = 1234567;
    frame.e2 = 42;
    frame.e3 = 255;
    frame.data.assign(1920 * 4, 0xab);
    frame.data3 = {1, 2, 3, 4, 5, 6};
    frame.e10 = 3;

    const shim::Bytes result = shim::FrameArrayResult(frame);
    shim::Value value;
    Check("the result parses as one complete msgpack value",
          shim::ParseValue(result.data(), result.size(), &value) == result.size());
    Check("the result is the 11-element array", value.is_array(11));
    if (!value.is_array(11)) return;

    const auto& e = value.array;
    Check("e0 is 0", e[0].kind == shim::Value::Kind::kUInt && e[0].u == 0);
    Check("e1 carries the source timestamp", e[1].kind == shim::Value::Kind::kUInt && e[1].u == 1234567);
    Check("e2 carries the second time word", e[2].kind == shim::Value::Kind::kUInt && e[2].u == 42);
    Check("e3 is the format", e[3].kind == shim::Value::Kind::kUInt && e[3].u == 255);
    Check("e4 is the whole-frame bin", e[4].kind == shim::Value::Kind::kBin && e[4].bin.size() == 1920 * 4);
    Check("e5 is e4's length", e[5].kind == shim::Value::Kind::kUInt && e[5].u == 1920 * 4);
    Check("e6 is the second plane", e[6].kind == shim::Value::Kind::kBin && e[6].bin.empty());
    Check("e7 is e6's length", e[7].kind == shim::Value::Kind::kUInt && e[7].u == 0);
    Check("e8 is the third plane", e[8].kind == shim::Value::Kind::kBin && e[8].bin.size() == 6);
    Check("e9 is e8's length", e[9].kind == shim::Value::Kind::kUInt && e[9].u == 6);
    const int64_t e10 = e[10].kind == shim::Value::Kind::kInt
                            ? e[10].i
                            : (e[10].kind == shim::Value::Kind::kUInt ? (int64_t)e[10].u : -999);
    Check("e10 is the IR light type", e10 == 3);

    shim::FramePayload parsed;
    Check("the array parses back into the frame words", shim::ParseFrameArray(value, &parsed));
    Check("the round trip keeps the frame bytes", parsed.data == frame.data && parsed.e1_us == frame.e1_us);

    shim::Value no_data;
    const shim::Bytes empty = shim::NoDataFrameArray();
    Check("the no-data form parses", shim::ParseValue(empty.data(), empty.size(), &no_data) && no_data.is_array(11));
    if (no_data.is_array(11)) {
        const auto& n = no_data.array;
        Check("the no-data form has three empty buffers",
              n[4].bin.empty() && n[6].bin.empty() && n[8].bin.empty());
        Check("the no-data form's lengths are zero", n[5].u == 0 && n[7].u == 0 && n[9].u == 0);
        Check("the no-data form ends in -1", n[10].kind == shim::Value::Kind::kInt && n[10].i == -1);
        Check("the no-data form keeps e3 at 255", n[3].u == 255);
    }
}

void TestDiscovery() {
    std::printf("discovery\n");
    const shim::Bytes command = shim::DiscoveryCommand();
    Check("the command is the spec's 8 bytes", command.size() == 8 && command[0] == 0x42 && command[3] == 0x02 &&
                                                   command[7] == 0x01);
    Check("the recorded command is recognised", shim::IsDiscoveryCommand(command.data(), command.size()));
    Check("a short datagram is not a command", !shim::IsDiscoveryCommand(command.data(), 7));
    shim::Bytes altered = command;
    altered[5] = 0x01;
    Check("an altered command is not recognised", !shim::IsDiscoveryCommand(altered.data(), altered.size()));
}

void TestVendorFraming() {
    std::printf("vendor framing\n");
    shim::PropertyBank bank;

    auto request = [](uint16_t opcode, uint32_t id, int32_t value, bool with_value) {
        shim::Bytes payload;
        for (int i = 0; i < 4; ++i) payload.push_back((uint8_t)(id >> (i * 8)));
        if (with_value) {
            for (int i = 0; i < 4; ++i) payload.push_back((uint8_t)((uint32_t)value >> (i * 8)));
        }
        shim::Bytes out = {0x47, 0x4d, (uint8_t)((8 + payload.size()) / 2), 0x00, (uint8_t)opcode, 0x00, 0x11,
                           0x00};
        out.insert(out.end(), payload.begin(), payload.end());
        return out;
    };

    shim::VendorRequest parsed;
    std::string err;
    const shim::Bytes get = request(1, 154, 0, false);
    Check("a GET_PROPERTY frame parses", shim::ParseVendorRequest(get, &parsed, &err));
    Check("the opcode and request id survive", parsed.opcode == 1 && parsed.request_id == 0x11);

    const shim::VendorResponse got = shim::HandleVendorRequest(parsed, &bank);
    Check("GET_PROPERTY answers without an error", got.error == 0);
    Check("the PropertyData is five i32", got.payload.size() == 20);
    if (got.payload.size() == 20) {
        auto i32 = [&](size_t at) {
            return (int32_t)((uint32_t)got.payload[at] | ((uint32_t)got.payload[at + 1] << 8) |
                             ((uint32_t)got.payload[at + 2] << 16) | ((uint32_t)got.payload[at + 3] << 24));
        };
        Check("prop 154 reads its cur/max/min/def/step",
              i32(0) == 1400000 && i32(4) == 1500000 && i32(8) == 100000 && i32(12) == 810000 &&
                  i32(16) == 5900);
    }

    shim::ParseVendorRequest(request(2, 3, 1, true), &parsed, &err);
    Check("a SET_PROPERTY is accepted", shim::HandleVendorRequest(parsed, &bank).error == 0);
    shim::ParseVendorRequest(request(1, 3, 0, false), &parsed, &err);
    const shim::VendorResponse after = shim::HandleVendorRequest(parsed, &bank);
    Check("a GET after the SET reads the value back", after.payload.size() == 20 && after.payload[0] == 1);

    shim::ParseVendorRequest(request(25, 0, 0, false), &parsed, &err);
    Check("an opcode the spec does not describe is refused, not guessed",
          shim::HandleVendorRequest(parsed, &bank).error != 0);

    const shim::VendorResponse response;
    const shim::Bytes wire = shim::BuildVendorResponse(response);
    Check("the response carries the BR magic", wire.size() >= 10 && wire[0] == 0x52 && wire[1] == 0x42);
    Check("the response's error code is 0 at offset 8", wire.size() >= 10 && wire[8] == 0 && wire[9] == 0);
}

void TestInterfaceTable() {
    std::printf("interface table\n");
    Check("IR is port 10013, suffix 0-2-12, stream type 6",
          shim::IrInterface().port == 10013 && shim::IrInterface().suffix == "0-2-12" &&
              shim::IrInterface().stream_type == 6);
    Check("depth is port 10010, suffix 0-2-10, stream type 7",
          shim::DepthInterface().port == 10010 && shim::DepthInterface().suffix == "0-2-10" &&
              shim::DepthInterface().stream_type == 7);
}

// A connection to the shim for the one check the client cannot make: an
// unknown method name.
bool SendRawRequest(uint16_t port, const shim::Bytes& request, shim::Bytes* reply) {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    struct timeval tv = {2, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    bool ok = connect(fd, (sockaddr*)&addr, sizeof(addr)) == 0;
    if (ok) ok = send(fd, request.data(), request.size(), 0) == (ssize_t)request.size();
    shim::Bytes buffer;
    if (ok) {
        uint8_t tmp[65536];
        for (;;) {
            shim::Value value;
            if (!buffer.empty() && shim::ParseValue(buffer.data(), buffer.size(), &value) == buffer.size()) break;
            const ssize_t n = recv(fd, tmp, sizeof(tmp), 0);
            if (n <= 0) {
                ok = false;
                break;
            }
            buffer.insert(buffer.end(), tmp, tmp + n);
        }
    }
    close(fd);
    if (ok && reply) *reply = buffer;
    return ok;
}

// Writes a small bundle the shim can serve: the recorder's own writer, so the
// format is not re-implemented for the test.
bool WriteSyntheticBundle(const std::string& dir, int frames, const std::string& method, uint16_t port,
                          std::string* err) {
    capture::Recorder recorder;
    if (!recorder.Open(dir, "raptor_shim_test", 60, 64ull * 1024 * 1024, err)) return false;
    for (int i = 0; i < frames; ++i) {
        shim::FramePayload frame;
        frame.e1_us = 1000000 + (uint64_t)i * 20000;
        frame.e2 = (uint64_t)i;
        frame.e3 = 255;
        frame.data.assign(1920 * 4, (uint8_t)i);
        frame.data3.assign(6, 0);
        frame.e10 = 2;
        const shim::Bytes result = shim::FrameArrayResult(frame);
        recorder.Record(port, method, (uint64_t)(i + 1), Envelope((uint64_t)(i + 1), result));
    }
    const std::string manifest = recorder.Close("test", "test");
    if (manifest.empty() || recorder.Failed()) {
        if (err) *err = "the synthetic bundle did not close";
        return false;
    }
    return true;
}

// Starts the shim on the vendor port, or on the switch's port when the vendor
// daemon holds it. Null with the reason when neither binds.
std::unique_ptr<shim::ShimServer> StartShim(shim::ShimConfig config, bool* switched, uint16_t* port,
                                            std::string* err) {
    *port = config.ir_port;
    std::unique_ptr<shim::ShimServer> server(new shim::ShimServer(config));
    if (server->Start(err)) return server;
    config.ir_port = openscanner::kShimIrPort;
    *port = config.ir_port;
    *switched = true;
    server.reset(new shim::ShimServer(config));
    if (server->Start(err)) return server;
    return nullptr;
}

void TestClientAgainstShim(const std::string& client_path) {
    std::printf("raptor_client --live against the shim\n");
    if (client_path.empty()) {
        std::printf("  SKIP (no raptor_client path given)\n");
        return;
    }

    char temp[] = "/tmp/raptor_shim_XXXXXX";
    const char* dir = mkdtemp(temp);
    if (!dir) {
        std::printf("  SKIP (mkdtemp failed)\n");
        return;
    }
    const std::string bundle = dir;
    std::string err;
    if (!WriteSyntheticBundle(bundle, 6, "ob_rpc_get_frame0-2-12", shim::IrInterface().port, &err)) {
        std::printf("  SKIP (the synthetic bundle could not be written: %s)\n", err.c_str());
        capture::RemoveTree(bundle);
        return;
    }

    shim::BundleSource source;
    if (!source.Open(bundle, shim::IrInterface(), &err)) {
        std::printf("  SKIP (the bundle source could not open: %s)\n", err.c_str());
        capture::RemoveTree(bundle);
        return;
    }

    shim::ShimConfig config;
    config.serve_depth = false;
    config.ir_port = 10013;  // the port the synthetic bundle was captured on
    config.ir_source = &source;
    std::vector<std::string> log;
    config.log = [&log](const std::string& line) { log.push_back(line); };

    bool switched = false;
    uint16_t port = 0;
    std::unique_ptr<shim::ShimServer> server = StartShim(config, &switched, &port, &err);
    if (!server) {
        // The vendor daemon may hold 10013; the switch's own port is the
        // answer, and the client's --daemon shim dials it.
        std::printf("  SKIP (the shim could not bind an IR port: %s)\n", err.c_str());
        capture::RemoveTree(bundle);
        return;
    }
    server->StartThread();
    const std::string daemon_flag = switched ? " --daemon shim" : "";

    const std::string out_path = bundle + "/frame.raw";
    const std::string log_path = bundle + "/client.log";
    const std::string command = "'" + client_path + "' --live 127.0.0.1 " + std::to_string(port) + " 2 '" +
                                out_path + "'" + daemon_flag + " --dump-replies > '" + log_path + "' 2>&1";
    const int rc = std::system(command.c_str());
    const int exit_code = WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
    // The client's own stats, before the deliberate unknown request below.
    const shim::ShimStats client_stats = server->stats();

    // The runs above prove the client's path; these two requests prove the
    // surface the client never calls, while the server is still listening.
    shim::Packer enum_request;
    enum_request.array(4);
    enum_request.uint(0);
    enum_request.uint(100);
    enum_request.str("ob_get_device_enumeration_info");
    enum_request.array(0);
    shim::Bytes enum_reply;
    const bool enum_reached = SendRawRequest(port, enum_request.out, &enum_reply);

    // The one method the client never sends: a bare name must be refused the
    // way the daemon refuses it.
    shim::Packer request;
    request.array(4);
    request.uint(0);
    request.uint(99);
    request.str("ob_rpc_get_frame");
    request.array(0);
    shim::Bytes reply;
    const bool raw_reached = SendRawRequest(port, request.out, &reply);

    server->Stop();
    server->Join();

    const std::string client_log = ReadFile(log_path);
    const shim::ShimStats stats = server->stats();
    Check("the client run exits 0", exit_code == 0);
    Check("the client pulled frames", client_stats.get_frame >= 3 && client_stats.frames >= 3);
    Check("the client started and stopped the stream",
          client_stats.start_stream == 1 && client_stats.stop_stream == 1);
    Check("every call carried the IR suffix (no unknown methods)", client_stats.unknown == 0);
    Check("the client saved the first frame",
          client_log.find("[live] saved first frame") != std::string::npos);
    Check("the client's own tally counts the frames as data",
          client_log.find("data=") != std::string::npos && CountAfter(client_log, "data=") > 0);
    Check("the client's tally reports no error replies", CountAfter(client_log, "error=") == 0);
    Check("the client's dump shows the 11-element array",
          client_log.find("result=array(11)") != std::string::npos);
    Check("the client's dump shows the whole-frame band",
          client_log.find("band(7680)") != std::string::npos);
    const std::string saved = ReadFile(out_path);
    Check("the shim served the bundle in order (the saved frame is frame 0)",
          saved.size() == 7680 && !saved.empty() && (uint8_t)saved[0] == 0 &&
              (uint8_t)saved[saved.size() - 1] == 0);

    Check("the enumeration request is answered", enum_reached);
    shim::Value enum_value;
    const bool enum_parsed = !enum_reply.empty() &&
                             shim::ParseValue(enum_reply.data(), enum_reply.size(), &enum_value) == enum_reply.size();
    Check("the enumeration reply is an envelope with an array of four",
          enum_parsed && enum_value.is_array(4) && enum_value.array[3].is_array(4));
    if (enum_parsed && enum_value.is_array(4) && enum_value.array[3].is_array(4)) {
        bool shaped = true;
        bool ports_ok = true;
        // The IR and depth entries carry the ports this shim is configured
        // with; IMU and RGB are the device's fixed ports it does not serve.
        const uint64_t expected[4] = {(uint64_t)openscanner::kShimDepthPort, 10011, 10012, (uint64_t)port};
        for (size_t i = 0; i < 4; ++i) {
            const shim::Value& entry = enum_value.array[3].array[i];
            if (!entry.is_array(8)) shaped = false;
            else if (entry.array[7].kind != shim::Value::Kind::kUInt || entry.array[7].u != expected[i]) {
                ports_ok = false;
            }
        }
        Check("every enumeration entry carries the eight fields", shaped);
        Check("the entries carry the configured port map", ports_ok);
    }

    Check("a raw request reaches the shim", raw_reached);
    shim::Value envelope;
    const bool parsed = !reply.empty() && shim::ParseValue(reply.data(), reply.size(), &envelope) == reply.size();
    Check("the refusal is a complete reply envelope", parsed && envelope.is_array(4));
    if (parsed && envelope.is_array(4)) {
        Check("the refusal names the method and its argument count",
              envelope.array[2].kind == shim::Value::Kind::kStr &&
                  envelope.array[2].str == "could not find function 'ob_rpc_get_frame' with argument count 0");
        Check("the refusal's result is nil", envelope.array[3].kind == shim::Value::Kind::kNil);
    }
    Check("the shim counted the unknown method", stats.unknown == 1);

    capture::RemoveTree(bundle);
}

// The no-data form end to end: a source with nothing to give must make the
// client report empty replies, not errors.
void TestEmptySource(const std::string& client_path) {
    std::printf("the no-data form against the client\n");
    if (client_path.empty()) {
        std::printf("  SKIP (no raptor_client path given)\n");
        return;
    }
    struct EmptySource : shim::FrameSource {
        bool Start(const shim::Interface&, std::string*) override { return true; }
        void Stop(const shim::Interface&) override {}
        bool Next(const shim::Interface&, shim::FramePayload*, std::string*) override { return false; }
        const char* Name() const override { return "empty"; }
    } source;

    shim::ShimConfig config;
    config.serve_depth = false;
    config.ir_port = 10013;
    config.ir_source = &source;
    std::string err;
    bool switched = false;
    uint16_t port = 0;
    std::unique_ptr<shim::ShimServer> server = StartShim(config, &switched, &port, &err);
    if (!server) {
        std::printf("  SKIP (the shim could not bind an IR port: %s)\n", err.c_str());
        return;
    }
    server->StartThread();
    const std::string daemon_flag = switched ? " --daemon shim" : "";

    char temp[] = "/tmp/raptor_shim_empty_XXXXXX";
    const char* dir = mkdtemp(temp);
    if (!dir) {
        server->Stop();
        server->Join();
        std::printf("  SKIP (mkdtemp failed)\n");
        return;
    }
    const std::string log_path = std::string(dir) + "/client.log";
    const std::string out_path = std::string(dir) + "/frame.raw";
    const std::string command = "'" + client_path + "' --live 127.0.0.1 " + std::to_string(port) + " 1 '" +
                                out_path + "'" + daemon_flag + " --dump-replies > '" + log_path + "' 2>&1";
    const int rc = std::system(command.c_str());
    const int exit_code = WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
    server->Stop();
    server->Join();

    const std::string client_log = ReadFile(log_path);
    Check("a run with no frames exits 1 (the client's no-data verdict)", exit_code == 1);
    Check("the client reports empty replies", CountAfter(client_log, "empty=") > 0);
    Check("the client reports no data and no errors",
          CountAfter(client_log, "data=") == 0 && CountAfter(client_log, "error=") == 0);
    Check("the shim answered the no-data form, not an error",
          server->stats().empty > 0 && server->stats().unknown == 0);

    capture::RemoveTree(std::string(dir));
}

// Row #179: the switch. The default is the vendor daemon and nothing about a
// default run changes; the shim's ports are its own so both can listen at once.
void TestDaemonSwitch() {
    std::printf("daemon switch\n");
    const openscanner::DaemonSelection vendor = openscanner::VendorDaemon();
    const openscanner::DaemonSelection shim = openscanner::ShimDaemon();
    Check("the default selection is the vendor daemon", vendor.kind == openscanner::DaemonKind::kVendor);
    Check("the vendor daemon keeps its ports", vendor.ir_port == 10013 && vendor.depth_port == 10010);
    Check("the shim's ports are its own, not the vendor's",
          shim.is_shim() && shim.ir_port != vendor.ir_port && shim.depth_port != vendor.depth_port);
    Check("the shim's defaults are the header's", shim.ir_port == openscanner::kShimIrPort &&
                                                      shim.depth_port == openscanner::kShimDepthPort);

    openscanner::DaemonSelection parsed;
    Check("'vendor' parses", openscanner::ParseDaemonName("vendor", &parsed) && !parsed.is_shim());
    Check("'shim' parses", openscanner::ParseDaemonName("shim", &parsed) && parsed.is_shim() &&
                               parsed.ir_port == openscanner::kShimIrPort);
    openscanner::DaemonSelection untouched = openscanner::VendorDaemon();
    Check("a typo is refused, not defaulted", !openscanner::ParseDaemonName("shimm", &untouched) &&
                                                   untouched.kind == openscanner::DaemonKind::kVendor);

    const std::string shim_line = openscanner::DescribeDaemon(shim);
    Check("the shim's line names the switch, its ports and that nothing was replaced",
          shim_line.find("daemon=shim") != std::string::npos &&
              shim_line.find("IR " + std::to_string(shim.ir_port)) != std::string::npos &&
              shim_line.find("neither stopped nor replaced") != std::string::npos);
    const std::string vendor_line = openscanner::DescribeDaemon(vendor);
    Check("the vendor's line names the product default",
          vendor_line.find("daemon=vendor") != std::string::npos &&
              vendor_line.find("product default") != std::string::npos);

    shim::ShimConfig defaults;
    Check("the shim server's defaults are the switch's shim ports",
          defaults.ir_port == openscanner::kShimIrPort && defaults.depth_port == openscanner::kShimDepthPort);
}

// The live seam without any device: every gate is a fake, so the refusals and
// the mapping are exact.
struct FakeTransport : raw_capture::FrameSource {
    bool configured_ = true;
    std::string configuration_error_ = "not-configured: the fake transport has no endpoint";
    bool has_frame_ = true;
    raw_capture::Frame next;

    FakeTransport() {
        next.kind = raw_capture::StreamKind::kIr;
        next.seq = 9;
        next.timestamp_us = 4242;  // the device PTS; deliberately not an OB word
        next.width = 1920;
        next.height = 1200;
        next.stride = 1920;
        next.format = 0x3231564e;  // a UVC fourcc, deliberately not an OBFormat
        next.data.assign(1920u * 1200u, 0x5a);  // the #174 whole frame
    }

    bool Next(raw_capture::Frame* frame, std::string* error) override {
        if (!has_frame_) {
            if (error) error->clear();  // the clean end
            return false;
        }
        has_frame_ = false;
        if (frame) *frame = next;
        return true;
    }
    bool configured() const override { return configured_; }
    std::string configuration_error() const override { return configuration_error_; }
};

struct FakeWords : shim::ObFrameWordsProvider {
    bool configured = true;
    std::string configured_why = "not-configured: e10 (the fake light type) has no source";
    bool supplied = true;
    shim::ObFrameWords value;

    FakeWords() {
        value.e1 = 1792552122036427ull;
        value.e2 = 211599394ull;
        value.e3 = 255;
        value.e10 = 4;
    }

    bool Configured(const shim::Interface&, std::string* why) const override {
        if (configured) return true;
        if (why) *why = configured_why;
        return false;
    }
    bool Words(const shim::Interface&, uint64_t, uint64_t, shim::ObFrameWords* out) const override {
        if (!supplied) return false;
        if (out) *out = value;
        return true;
    }
};

struct FakeClocks : shim::HostClocks {
    uint64_t realtime = 1792552122036427ull;
    uint64_t monotonic = 211599394ull;
    uint64_t RealtimeUs() const override { return realtime; }
    uint64_t MonotonicUs() const override { return monotonic; }
};

struct FakeLight : shim::IrLightState {
    bool supplied = true;
    int32_t value = 4;
    bool LightType(const shim::Interface&, int32_t* out, std::string* why) const override {
        if (!supplied) {
            if (why) *why = "not-configured: e10 (the fake light type) has no state";
            return false;
        }
        if (out) *out = value;
        return true;
    }
};

struct FakeClaim : shim::ClaimChannel {
    State state = State::kClaimedByHelper;
    std::string detail = "claimed (fake)";
    State Query(std::string* out) override {
        if (out) *out = detail;
        return state;
    }
};

void TestLiveSeam() {
    std::printf("live source seam\n");
    FakeTransport transport;
    FakeWords words;
    FakeClaim claim;
    std::string err;

    {
        FakeClaim unclaimed;
        unclaimed.state = FakeClaim::State::kNotClaimed;
        unclaimed.detail = "openscanner-helper reports released";
        shim::LiveSource source(&transport, &words, &unclaimed);
        Check("an unclaimed device is refused", !source.Start(shim::IrInterface(), &err));
        Check("the refusal is not-configured and names the helper",
              err.find("not-configured") != std::string::npos &&
                  err.find("openscanner-helper") != std::string::npos);
    }
    {
        transport.configured_ = false;
        shim::LiveSource source(&transport, &words, &claim);
        Check("an unconfigured transport is refused", !source.Start(shim::IrInterface(), &err));
        Check("the transport's own reason is carried", err.find("no endpoint") != std::string::npos);
        transport.configured_ = true;
    }
    {
        shim::LiveSource source(&transport, nullptr, &claim);
        Check("missing OB words are refused", !source.Start(shim::IrInterface(), &err));
        Check("the refusal names the OB words", err.find("OB words") != std::string::npos);
    }
    {
        words.configured = false;
        shim::LiveSource source(&transport, &words, &claim);
        Check("a provider that cannot supply a word is refused", !source.Start(shim::IrInterface(), &err));
        Check("the refusal is the provider's, by name (e10)", err.find("not-configured: e10") != std::string::npos);
        words.configured = true;
    }
    {
        shim::LiveSource source(&transport, &words, &claim);
        Check("the configured seam starts", source.Start(shim::IrInterface(), &err));

        shim::FramePayload payload;
        Check("a frame crosses the seam", source.Next(shim::IrInterface(), &payload, &err));
        Check("the payload is the frame's bytes", payload.data == transport.next.data);
        Check("the payload is the whole 1920x1200 frame", payload.data.size() == 2304000);
        Check("e1/e2 come from the provider's clocks, not the device PTS",
              payload.e1_us == words.value.e1 && payload.e2 == words.value.e2 &&
                  payload.e1_us != transport.next.timestamp_us);
        Check("e3 is the OBFormat, not the UVC fourcc",
              payload.e3 == 255 && payload.e3 != transport.next.format);
        Check("e10 is the light type the provider supplied", payload.e10 == 4);
        Check("the absent sibling planes stay empty", payload.data2.empty() && payload.data3.empty());

        // The #174 array: the whole frame in e4, the provider's words beside it.
        const shim::Bytes result = shim::FrameArrayResult(payload);
        shim::Value value;
        Check("the frame array parses as one value",
              shim::ParseValue(result.data(), result.size(), &value) == result.size());
        Check("the result is the 11-element array", value.is_array(11));
        if (value.is_array(11)) {
            const auto& e = value.array;
            Check("e0 is 0", e[0].u == 0);
            Check("e1 is CLOCK_REALTIME us", e[1].u == words.value.e1);
            Check("e2 is CLOCK_MONOTONIC us", e[2].u == words.value.e2);
            Check("e3 is 255", e[3].u == 255);
            Check("e4 is the whole frame", e[4].kind == shim::Value::Kind::kBin && e[4].bin.size() == 2304000);
            Check("e5 is e4's length", e[5].u == 2304000);
            const int64_t e10 = e[10].kind == shim::Value::Kind::kInt
                                    ? e[10].i
                                    : (e[10].kind == shim::Value::Kind::kUInt ? (int64_t)e[10].u : -999);
            Check("e10 is the light type", e10 == 4);
        }

        Check("the end of the stream is a clean stop, not an error",
              !source.Next(shim::IrInterface(), &payload, &err) && err.empty());

        transport.has_frame_ = true;  // a frame to map, but no words for it
        words.supplied = false;
        Check("a frame without OB words is refused, not filled in",
              !source.Next(shim::IrInterface(), &payload, &err) &&
                  err.find("not-configured") != std::string::npos);
        words.supplied = true;
    }
    {
        // The settled provider (row #180): host clocks at dequeue, the observed
        // OBFormat, and e10 from a real light source — or a refusal by name.
        FakeClocks clocks;
        FakeLight light;
        shim::HostClockObWords provider(&clocks, &light);
        shim::ObFrameWords out;
        Check("the provider is configured with clocks and a light source",
              provider.Configured(shim::IrInterface(), &err));
        Check("the provider supplies every word",
              provider.Words(shim::IrInterface(), 9, 4242, &out));
        Check("e1 is the host realtime clock", out.e1 == clocks.realtime);
        Check("e2 is the host monotonic clock", out.e2 == clocks.monotonic);
        Check("e3 is the observed OBFormat 255", out.e3 == 255);
        Check("e10 is the light source's value", out.e10 == light.value);
        Check("the device PTS is not written into e1 or e2",
              out.e1 != 4242 && out.e2 != 4242);

        FakeLight unwired;
        unwired.supplied = false;
        shim::HostClockObWords refused(&clocks, &unwired);
        Check("a missing light source refuses by name", !refused.Configured(shim::IrInterface(), &err));
        Check("the refusal names e10", err.find("not-configured: e10") != std::string::npos);
        shim::HostClockObWords no_light(&clocks, nullptr);
        Check("no light source at all refuses by name", !no_light.Configured(shim::IrInterface(), &err));
        Check("the no-source refusal names e10", err.find("not-configured: e10") != std::string::npos);
    }
    {
        // The production factory: #178's transport is not landed and e10 has
        // no source, so it refuses with every missing input named; with no
        // claim it names the helper too.
        FakeClaim unclaimed;
        unclaimed.state = FakeClaim::State::kNotClaimed;
        unclaimed.detail = "openscanner-helper reports released";
        std::string why;
        std::unique_ptr<shim::LiveSource> source(shim::LiveSourceFactory::Create(&unclaimed, &why));
        Check("the production live source refuses for now", source == nullptr);
        Check("the refusal names the descriptor map", why.find("usb-descriptor-map.md") != std::string::npos);
        Check("the refusal names the helper claim", why.find("openscanner-helper") != std::string::npos);
        Check("the refusal names e10's missing source", why.find("not-configured: e10") != std::string::npos);
    }
}

// The switch-on path end to end: the shim on its own port, the client told to
// use it. The switch-off path is the same client on the same port without the
// flag, which must be refused as an unknown interface port.
void TestSwitchOnPath(const std::string& client_path) {
    std::printf("switch on: the client against the shim\n");
    if (client_path.empty()) {
        std::printf("  SKIP (no raptor_client path given)\n");
        return;
    }
    char temp[] = "/tmp/raptor_switch_XXXXXX";
    const char* dir = mkdtemp(temp);
    if (!dir) {
        std::printf("  SKIP (mkdtemp failed)\n");
        return;
    }
    const std::string root = dir;
    std::string err;
    if (!WriteSyntheticBundle(root, 4, "ob_rpc_get_frame0-2-12", 10013, &err)) {
        std::printf("  SKIP (the synthetic bundle could not be written: %s)\n", err.c_str());
        capture::RemoveTree(root);
        return;
    }
    shim::BundleSource source;
    if (!source.Open(root, shim::IrInterface(), &err)) {
        std::printf("  SKIP (the bundle source could not open: %s)\n", err.c_str());
        capture::RemoveTree(root);
        return;
    }
    shim::ShimConfig config;
    config.serve_depth = false;
    config.ir_port = openscanner::kShimIrPort;
    config.ir_source = &source;
    shim::ShimServer server(config);
    if (!server.Start(&err)) {
        std::printf("  SKIP (the shim could not bind its port: %s)\n", err.c_str());
        capture::RemoveTree(root);
        return;
    }
    server.StartThread();

    const std::string out_path = root + "/switched.raw";
    const std::string log_path = root + "/switched.log";
    const std::string command = "'" + client_path + "' --live 127.0.0.1 " +
                                std::to_string(openscanner::kShimIrPort) + " 2 '" + out_path +
                                "' --daemon shim --frames 4 --dump-replies > '" + log_path + "' 2>&1";
    const int rc = std::system(command.c_str());
    const int exit_code = WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;

    // The switch off: the same client, the same shim port, no flag. The
    // default table has no such interface port, so the client refuses.
    const std::string off_log = root + "/off.log";
    const std::string off_out = root + "/off.raw";
    const std::string off_command = "'" + client_path + "' --live 127.0.0.1 " +
                                    std::to_string(openscanner::kShimIrPort) + " 1 '" + off_out +
                                    "' > '" + off_log + "' 2>&1";
    const int off_rc = std::system(off_command.c_str());
    const int off_exit = WIFEXITED(off_rc) ? WEXITSTATUS(off_rc) : -1;

    server.Stop();
    server.Join();
    const shim::ShimStats stats = server.stats();
    const std::string log = ReadFile(log_path);
    const std::string off_text = ReadFile(off_log);

    Check("the switched client exits 0", exit_code == 0);
    Check("the switch is logged by name", log.find("daemon=shim") != std::string::npos);
    Check("the log names the shim's port", log.find("IR " + std::to_string(openscanner::kShimIrPort)) !=
                                               std::string::npos);
    Check("the log says the vendor daemon was not replaced",
          log.find("neither stopped nor replaced") != std::string::npos);
    Check("the shim served the frames on its own port", stats.frames >= 4 && stats.get_frame >= 4);
    Check("the client pulled them", log.find("data=") != std::string::npos && CountAfter(log, "data=") > 0);

    Check("with the switch off the same port is refused (no default change)", off_exit == 2);
    Check("the refusal names the port as unknown",
          off_text.find("is not an interface port") != std::string::npos);

    capture::RemoveTree(root);
}

// The CLI's live mode with no #178 transport: a clean refusal, not a guessed
// device and not a hang.
void TestLiveCliRefusal(const std::string& shim_path) {
    std::printf("live mode refuses without the transport\n");
    if (shim_path.empty()) {
        std::printf("  SKIP (no raptor_shim path given)\n");
        return;
    }
    char temp[] = "/tmp/raptor_live_refusal_XXXXXX";
    const char* dir = mkdtemp(temp);
    if (!dir) {
        std::printf("  SKIP (mkdtemp failed)\n");
        return;
    }
    const std::string log_path = std::string(dir) + "/shim.log";
    const std::string command =
        "'" + shim_path + "' --source live --no-depth --ir-port 20013 --seconds 1 > '" + log_path + "' 2>&1";
    const int rc = std::system(command.c_str());
    const int exit_code = WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
    const std::string text = ReadFile(log_path);
    Check("the live CLI exits 2", exit_code == 2);
    Check("it names the refusal", text.find("live source refused") != std::string::npos);
    Check("the refusal says not-configured", text.find("not-configured") != std::string::npos);
    Check("it names the missing descriptor map",
          text.find("usb-descriptor-map.md") != std::string::npos);
    Check("it names the helper claim", text.find("openscanner-helper") != std::string::npos);
    capture::RemoveTree(std::string(dir));
}

}  // namespace

int main(int argc, char** argv) {
    const std::string client_path = argc > 1 ? argv[1] : "";
    const std::string shim_path = argc > 2 ? argv[2] : "";
    TestFrameArray();
    TestDiscovery();
    TestVendorFraming();
    TestInterfaceTable();
    TestDaemonSwitch();
    TestLiveSeam();
    TestClientAgainstShim(client_path);
    TestEmptySource(client_path);
    TestSwitchOnPath(client_path);
    TestLiveCliRefusal(shim_path);

    std::printf("raptor_shim_test: %d check(s), %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
