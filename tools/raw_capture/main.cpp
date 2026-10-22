// Row #171 (Phase D seed 3): the raw capture CLI.
//
//   raw_capture --table
//       Print the descriptor table: what is known (identity, logical RPC
//       ports) and what the operator's USB capture must fill (interfaces,
//       endpoints, formats, HID/vendor control).
//
//   raw_capture --capture [--mock] --sink <file> [--stream ir|depth|both]
//                [--max-frames N] [--max-bytes N] [--max-seconds N]
//       Capture IR/depth frames to a local file. Without --mock the UVC
//       source refuses: the descriptor table is not configured yet, and the
//       tool will not guess an endpoint.
//
//   raw_capture --verify <file>
//       Read a capture back: records, per-stream counts, format/stride.
//
//   raw_capture --laser on|off | --exposure <value>
//       The #101 command sequences over the configured transport. Until the
//       HID/vendor control map is captured the transport refuses
//       (not-configured); no report is fabricated.
//
// Exit codes: 0 ok, 1 usage, 2 not-configured / I/O failure, 3 command refused
// by the transport (rejected/timeout).
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <thread>
#include <string>
#include <vector>

#include "correlation.h"
#include "descriptor_probe.h"
#include "hid_report_descriptor.h"
#include "raw_capture.h"

namespace rc = raw_capture;

namespace {

void PrintUsage() {
    std::printf(
        "Usage:\n"
        "  raw_capture --table\n"
        "  raw_capture --probe [--descriptor-file <blob>]\n"
        "  raw_capture --probe --match [--descriptor-file <blob>]\n"
        "  raw_capture --probe --watch [--seconds N] [--descriptor-file <blob>]\n"
        "  raw_capture --hid-report <saved-report-descriptor>\n"
        "  raw_capture --capture [--mock] --sink <file> [--stream ir|depth|rgb|both]\n"
        "              [--max-frames N] [--max-bytes N] [--max-seconds N]\n"
        "  raw_capture --verify <file>\n"
        "  raw_capture --laser on|off\n"
        "  raw_capture --exposure <value>\n");
}

void PrintTable() {
    const rc::DescriptorTable& table = rc::Table();
    std::printf("[table] identity: VID 0x%04X PID 0x%04X (%s)\n", table.vendor_id, table.product_id,
                rc::KnowledgeName(table.identity_knowledge));
    const rc::StreamSpec streams[rc::kStreamCount] = {table.ir, table.depth, table.rgb};
    for (const rc::StreamSpec& spec : streams) {
        std::printf("[table] stream %-5s rpc port %u (%s, suffix %s)\n", spec.name, spec.rpc_port,
                    rc::KnowledgeName(spec.rpc_port_knowledge), spec.rpc_suffix);
        std::printf("[table]   usb endpoint: %s", rc::KnowledgeName(spec.endpoint.knowledge));
        if (spec.endpoint.knowledge == rc::Knowledge::kKnown) {
            std::printf(" interface_number=%d alternate_setting=%d endpoint_address=0x%02X transfer=%s "
                        "max_packet_size=%d",
                        spec.endpoint.interface_number, spec.endpoint.alternate_setting,
                        spec.endpoint.endpoint_address, rc::TransferTypeName(spec.endpoint.transfer),
                        spec.endpoint.max_packet_size);
        }
        std::printf("\n");
        std::printf("[table]   format/stride: %s", spec.pixel_format != 0 ? "known" : "unknown");
        if (spec.pixel_format != 0) {
            std::printf(" pixel_format=0x%08X bytes_per_line=%u width=%u height=%u", spec.pixel_format,
                        spec.bytes_per_line, spec.width, spec.height);
        }
        std::printf("\n");
    }
    std::printf("[table] rpc ports: rgb %u (%s), imu %u (%s)\n", table.rgb_rpc_port,
                rc::KnowledgeName(table.rgb_rpc_port_knowledge), table.imu_rpc_port,
                rc::KnowledgeName(table.imu_rpc_port_knowledge));
    std::printf("[table] control (laser/exposure): endpoint %s",
                rc::KnowledgeName(table.control.endpoint.knowledge));
    if (table.control.endpoint.knowledge == rc::Knowledge::kKnown) {
        std::printf(" interface_number=%d alternate_setting=%d endpoint_address=0x%02X transfer=%s "
                    "max_packet_size=%d",
                    table.control.endpoint.interface_number, table.control.endpoint.alternate_setting,
                    table.control.endpoint.endpoint_address,
                    rc::TransferTypeName(table.control.endpoint.transfer),
                    table.control.endpoint.max_packet_size);
    }
    std::printf("\n");
    std::printf("[table] control report: descriptor_length=%d report_id=%s report_length=%d (%s)\n",
                table.control.report_descriptor_length,
                table.control.report_id == rc::kUnset ? "unknown" : "known", table.control.report_length,
                table.control.missing);
    std::printf("[table] note: IR/depth/RGB are assigned by the #182 interface-name mapping "
                "(IR=3, depth=1, RGB=5)\n");
}

rc::StreamKind ParseKind(const std::string& name, bool* ok) {
    *ok = true;
    if (name == "ir") return rc::StreamKind::kIr;
    if (name == "depth") return rc::StreamKind::kDepth;
    if (name == "rgb") return rc::StreamKind::kRgb;
    *ok = false;
    return rc::StreamKind::kIr;
}

int RunCapture(int argc, char** argv) {
    bool mock = false;
    std::string sink_path;
    std::string stream_name = "ir";
    rc::Limits limits;
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--mock") {
            mock = true;
        } else if (arg == "--sink" && i + 1 < argc) {
            sink_path = argv[++i];
        } else if (arg == "--stream" && i + 1 < argc) {
            stream_name = argv[++i];
        } else if (arg == "--max-frames" && i + 1 < argc) {
            limits.max_frames = std::strtoull(argv[++i], nullptr, 10);
        } else if (arg == "--max-bytes" && i + 1 < argc) {
            limits.max_bytes = std::strtoull(argv[++i], nullptr, 10);
        } else if (arg == "--max-seconds" && i + 1 < argc) {
            limits.max_duration_ms = static_cast<uint64_t>(std::atof(argv[++i]) * 1000.0);
        } else {
            std::fprintf(stderr, "unknown argument: %s\n", arg.c_str());
            return 1;
        }
    }
    if (sink_path.empty()) {
        std::fprintf(stderr, "--capture needs --sink <file>\n");
        return 1;
    }

    rc::FileSink sink;
    std::string error;
    if (!sink.Open(sink_path, &error)) {
        std::fprintf(stderr, "[capture] error: %s\n", error.c_str());
        return 2;
    }

    std::vector<rc::StreamKind> kinds;
    bool ok = true;
    if (stream_name == "both") {
        kinds = {rc::StreamKind::kIr, rc::StreamKind::kDepth};
    } else {
        kinds = {ParseKind(stream_name, &ok)};
        if (!ok) {
            std::fprintf(stderr, "--stream takes ir, depth, rgb or both\n");
            return 1;
        }
    }

    // One sink, one byte budget: the streams into a single file share it.
    uint64_t remaining_bytes = limits.max_bytes;
    uint64_t total_frames = 0;
    uint64_t total_bytes = 0;
    uint64_t per_stream[rc::kStreamCount] = {};
    rc::StopReason reason = rc::StopReason::kSourceEnded;
    std::string detail;

    for (rc::StreamKind kind : kinds) {
        rc::Limits run_limits = limits;
        run_limits.max_bytes = remaining_bytes;
        std::unique_ptr<rc::FrameSource> source;
        if (mock) {
            const bool depth = kind == rc::StreamKind::kDepth;
            const bool rgb = kind == rc::StreamKind::kRgb;
            // Fixed format/stride for the mock: 8-bit grey IR, 16-bit depth, YUY2-sized RGB.
            source = std::make_unique<rc::MockSource>(
                kind, 64, 48, rgb ? 0x32595559u : (depth ? 0x5A313620u : 0x59455247u), 0,
                run_limits.max_frames);
        } else {
            std::string why;
            source = rc::MakeLiveUvcSource(kind, &why);
            if (!source) {
                reason = rc::StopReason::kNotConfigured;
                detail = why;
                break;
            }
        }
        rc::CaptureRunner runner;
        const rc::CaptureSummary summary = runner.Run(*source, sink, run_limits);
        total_frames += summary.frames;
        total_bytes += summary.bytes;
        for (int s = 0; s < rc::kStreamCount; ++s) per_stream[s] += summary.per_stream[s];
        remaining_bytes = summary.bytes < remaining_bytes ? remaining_bytes - summary.bytes : 0;
        reason = summary.reason;
        detail = summary.detail;
        if (summary.reason == rc::StopReason::kNotConfigured) break;
        if (!summary.ok()) break;
    }
    sink.Close();

    std::printf("[capture] sink: %s (%llu bytes)\n", sink_path.c_str(),
                static_cast<unsigned long long>(sink.bytes_written()));
    std::printf("[capture] stop: %s\n", rc::StopReasonName(reason));
    if (!detail.empty()) std::printf("[capture] detail: %s\n", detail.c_str());
    std::printf("[capture] frames: %llu (ir %llu, depth %llu, rgb %llu), %llu record bytes\n",
                static_cast<unsigned long long>(total_frames),
                static_cast<unsigned long long>(per_stream[0]),
                static_cast<unsigned long long>(per_stream[1]),
                static_cast<unsigned long long>(per_stream[2]),
                static_cast<unsigned long long>(total_bytes));
    if (reason == rc::StopReason::kNotConfigured) return 2;
    return (reason == rc::StopReason::kSourceError || reason == rc::StopReason::kSinkError) ? 2 : 0;
}

int RunProbe(int argc, char** argv) {
    std::string descriptor_file;
    bool match = false;
    bool watch = false;
    double watch_seconds = 10.0;
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--descriptor-file" && i + 1 < argc) {
            descriptor_file = argv[++i];
        } else if (arg == "--match") {
            match = true;
        } else if (arg == "--watch") {
            watch = true;
        } else if (arg == "--seconds" && i + 1 < argc) {
            watch_seconds = std::atof(argv[++i]);
        } else {
            std::fprintf(stderr, "unknown argument: %s\n", arg.c_str());
            return 1;
        }
    }
    if (match || watch) {
        // The correlation path: the IORegistry read only. It works while the
        // vendor daemon holds the device (no open, no claim), so the operator
        // can start one RPC stream and watch which VS reacts.
        rc::ProbeResult descriptor;
        bool have_descriptor = false;
        std::string error;
        if (!descriptor_file.empty()) {
            std::ifstream in(descriptor_file, std::ios::binary);
            if (!in) {
                std::fprintf(stderr, "[match] error: cannot open %s\n", descriptor_file.c_str());
                return 2;
            }
            const std::vector<uint8_t> blob((std::istreambuf_iterator<char>(in)),
                                            std::istreambuf_iterator<char>());
            if (!rc::ParseConfigurationDescriptor(blob.data(), blob.size(), &descriptor, &error)) {
                std::fprintf(stderr, "[match] error: %s\n", descriptor.error.c_str());
                return 2;
            }
            have_descriptor = true;
        }
        std::vector<rc::InterfaceObservation> interfaces;
        if (!rc::ReadScannerInterfaces(&interfaces, &error)) {
            std::fprintf(stderr, "[match] error: %s\n", error.c_str());
            return 2;
        }
        if (watch) {
            rc::WatchTracker tracker;
            const auto end = std::chrono::steady_clock::now() +
                             std::chrono::milliseconds(static_cast<int64_t>(watch_seconds * 1000.0));
            while (std::chrono::steady_clock::now() < end) {
                std::string read_error;
                if (rc::ReadScannerInterfaces(&interfaces, &read_error)) {
                    for (const std::string& line : tracker.Update(interfaces)) {
                        std::printf("[watch] %s\n", line.c_str());
                    }
                } else {
                    std::fprintf(stderr, "[watch] %s\n", read_error.c_str());
                }
                std::fflush(stdout);
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
            }
        }
        const rc::CorrelationResult correlated =
            rc::Correlate(interfaces, have_descriptor ? &descriptor : nullptr);
        rc::PrintCorrelation(correlated, stdout);
        return correlated.error.empty() ? 0 : 2;
    }
    rc::ProbeResult result;
    std::string error;
    if (!descriptor_file.empty()) {
        std::ifstream in(descriptor_file, std::ios::binary);
        if (!in) {
            std::fprintf(stderr, "[probe] error: cannot open %s\n", descriptor_file.c_str());
            return 2;
        }
        const std::vector<uint8_t> blob((std::istreambuf_iterator<char>(in)),
                                        std::istreambuf_iterator<char>());
        if (!rc::ParseConfigurationDescriptor(blob.data(), blob.size(), &result, &error)) {
            std::fprintf(stderr, "[probe] error: %s\n", result.error.c_str());
            return 2;
        }
    } else if (!rc::ProbeAttachedDevice(&result)) {
        std::fprintf(stderr, "[probe] error: %s\n", result.error.c_str());
        return 2;
    }
    rc::PrintProbe(result, stdout);
    // Row #189: the HID report descriptor, always in the probe section.
    if (!result.hid_report.empty()) {
        const rc::HidReportDescriptor hid =
            rc::ParseHidReportDescriptor(result.hid_report.data(), result.hid_report.size());
        rc::PrintHidReportDescriptor(hid, "", stdout);
    } else {
        rc::PrintHidReportDescriptor(rc::HidReportDescriptor{}, result.hid_report_error, stdout);
    }
    return 0;
}

int RunHidReportFile(const std::string& path) {
    std::vector<uint8_t> bytes;
    std::string error;
    if (!rc::ReadHidReportFile(path, &bytes, &error)) {
        std::fprintf(stderr, "[hid] error: %s\n", error.c_str());
        return 2;
    }
    const rc::HidReportDescriptor parsed = rc::ParseHidReportDescriptor(bytes.data(), bytes.size());
    rc::PrintHidReportDescriptor(parsed, "", stdout);
    return parsed.ok() ? 0 : 2;
}

int RunVerify(const std::string& path) {
    std::vector<rc::CapturedRecord> records;
    std::string error;
    if (!rc::ReadCaptureFile(path, &records, &error)) {
        std::fprintf(stderr, "[verify] error: %s\n", error.c_str());
        return 2;
    }
    uint64_t per_stream[rc::kStreamCount] = {};
    for (const rc::CapturedRecord& record : records) {
        per_stream[static_cast<int>(record.frame.kind)]++;
    }
    std::printf("[verify] %s: %zu record(s) (ir %llu, depth %llu, rgb %llu)\n", path.c_str(), records.size(),
                static_cast<unsigned long long>(per_stream[0]),
                static_cast<unsigned long long>(per_stream[1]),
                static_cast<unsigned long long>(per_stream[2]));
    for (const rc::CapturedRecord& record : records) {
        std::printf("[verify] seq %llu kind %s format 0x%08X %ux%u stride %u bytes %zu\n",
                    static_cast<unsigned long long>(record.frame.seq),
                    record.frame.kind == rc::StreamKind::kIr     ? "ir"
                    : record.frame.kind == rc::StreamKind::kDepth ? "depth"
                                                                  : "rgb",
                    record.frame.format,
                    record.frame.width, record.frame.height, record.frame.stride,
                    record.frame.data.size());
    }
    return 0;
}

int RunCommand(int argc, char** argv) {
    std::string why;
    std::unique_ptr<rc::CommandTransport> transport = rc::MakeLiveHidTransport(&why);
    if (!transport) {
        std::printf("[command] %s\n", why.c_str());
        return 2;
    }
    rc::VendorCommandPath path(transport.get());
    rc::CommandResult result;
    if (std::strcmp(argv[1], "--laser") == 0) {
        if (argc < 3) {
            std::fprintf(stderr, "--laser needs on|off\n");
            return 1;
        }
        const std::string value = argv[2];
        if (value != "on" && value != "off") {
            std::fprintf(stderr, "--laser takes on or off\n");
            return 1;
        }
        result = path.SetLaser(value == "on");
    } else {
        if (argc < 3) {
            std::fprintf(stderr, "--exposure needs a value\n");
            return 1;
        }
        result = path.SetExposure(std::atof(argv[2]));
    }
    std::printf("[command] %s: property %u attempt %d (%s)\n", rc::CommandStatusName(result.status),
                result.property, result.attempt, result.detail.c_str());
    if (result.ok()) return 0;
    return result.status == rc::CommandStatus::kNotConfigured ? 2 : 3;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        PrintUsage();
        return 1;
    }
    const std::string command = argv[1];
    if (command == "--table") {
        PrintTable();
        return 0;
    }
    if (command == "--probe") return RunProbe(argc, argv);
    if (command == "--hid-report") {
        if (argc < 3) {
            std::fprintf(stderr, "--hid-report needs a saved report-descriptor file\n");
            return 1;
        }
        return RunHidReportFile(argv[2]);
    }
    if (command == "--capture") return RunCapture(argc, argv);
    if (command == "--verify") {
        if (argc < 3) {
            std::fprintf(stderr, "--verify needs a capture file\n");
            return 1;
        }
        return RunVerify(argv[2]);
    }
    if (command == "--laser" || command == "--exposure") return RunCommand(argc, argv);
    if (command == "--help" || command == "-h") {
        PrintUsage();
        return 0;
    }
    PrintUsage();
    return 1;
}
