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
#include "bundle_source.h"
#include "discovery.h"
#include "frame_source.h"
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
    config.ir_source = &source;
    std::vector<std::string> log;
    config.log = [&log](const std::string& line) { log.push_back(line); };
    shim::ShimServer server(config);
    if (!server.Start(&err)) {
        std::printf("  SKIP (the shim could not bind the IR port: %s)\n", err.c_str());
        capture::RemoveTree(bundle);
        return;
    }
    server.StartThread();

    const std::string out_path = bundle + "/frame.raw";
    const std::string log_path = bundle + "/client.log";
    const std::string command = "'" + client_path + "' --live 127.0.0.1 10013 2 '" + out_path +
                                "' --dump-replies > '" + log_path + "' 2>&1";
    const int rc = std::system(command.c_str());
    const int exit_code = WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
    // The client's own stats, before the deliberate unknown request below.
    const shim::ShimStats client_stats = server.stats();

    // The runs above prove the client's path; these two requests prove the
    // surface the client never calls, while the server is still listening.
    shim::Packer enum_request;
    enum_request.array(4);
    enum_request.uint(0);
    enum_request.uint(100);
    enum_request.str("ob_get_device_enumeration_info");
    enum_request.array(0);
    shim::Bytes enum_reply;
    const bool enum_reached = SendRawRequest(config.ir_port, enum_request.out, &enum_reply);

    // The one method the client never sends: a bare name must be refused the
    // way the daemon refuses it.
    shim::Packer request;
    request.array(4);
    request.uint(0);
    request.uint(99);
    request.str("ob_rpc_get_frame");
    request.array(0);
    shim::Bytes reply;
    const bool raw_reached = SendRawRequest(config.ir_port, request.out, &reply);

    server.Stop();
    server.Join();

    const std::string client_log = ReadFile(log_path);
    const shim::ShimStats stats = server.stats();
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

    // The enumeration: the port map the app bootstraps from.
    Check("the enumeration request is answered", enum_reached);
    shim::Value enum_value;
    const bool enum_parsed = !enum_reply.empty() &&
                             shim::ParseValue(enum_reply.data(), enum_reply.size(), &enum_value) == enum_reply.size();
    Check("the enumeration reply is an envelope with an array of four", enum_parsed &&
              enum_value.is_array(4) && enum_value.array[3].is_array(4));
    if (enum_parsed && enum_value.is_array(4) && enum_value.array[3].is_array(4)) {
        bool shaped = true;
        bool ports_ok = true;
        const uint64_t expected[4] = {10010, 10011, 10012, 10013};
        for (size_t i = 0; i < 4; ++i) {
            const shim::Value& entry = enum_value.array[3].array[i];
            if (!entry.is_array(8)) shaped = false;
            else if (entry.array[7].kind != shim::Value::Kind::kUInt || entry.array[7].u != expected[i]) {
                ports_ok = false;
            }
        }
        Check("every enumeration entry carries the eight fields", shaped);
        Check("the entries carry the port map 10010/10011/10012/10013", ports_ok);
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
    Check("the shim counted the unknown method", server.stats().unknown == 1);

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
    config.ir_source = &source;
    shim::ShimServer server(config);
    std::string err;
    if (!server.Start(&err)) {
        std::printf("  SKIP (the shim could not bind the IR port: %s)\n", err.c_str());
        return;
    }
    server.StartThread();

    char temp[] = "/tmp/raptor_shim_empty_XXXXXX";
    const char* dir = mkdtemp(temp);
    if (!dir) {
        server.Stop();
        server.Join();
        std::printf("  SKIP (mkdtemp failed)\n");
        return;
    }
    const std::string log_path = std::string(dir) + "/client.log";
    const std::string out_path = std::string(dir) + "/frame.raw";
    const std::string command = "'" + client_path + "' --live 127.0.0.1 10013 1 '" + out_path +
                                "' --dump-replies > '" + log_path + "' 2>&1";
    const int rc = std::system(command.c_str());
    const int exit_code = WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
    server.Stop();
    server.Join();

    const std::string client_log = ReadFile(log_path);
    Check("a run with no frames exits 1 (the client's no-data verdict)", exit_code == 1);
    Check("the client reports empty replies", CountAfter(client_log, "empty=") > 0);
    Check("the client reports no data and no errors",
          CountAfter(client_log, "data=") == 0 && CountAfter(client_log, "error=") == 0);
    Check("the shim answered the no-data form, not an error", server.stats().empty > 0 && server.stats().unknown == 0);

    capture::RemoveTree(std::string(dir));
}

}  // namespace

int main(int argc, char** argv) {
    const std::string client_path = argc > 1 ? argv[1] : "";
    TestFrameArray();
    TestDiscovery();
    TestVendorFraming();
    TestInterfaceTable();
    TestClientAgainstShim(client_path);
    TestEmptySource(client_path);

    std::printf("raptor_shim_test: %d check(s), %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
