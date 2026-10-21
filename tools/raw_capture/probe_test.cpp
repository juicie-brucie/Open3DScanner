// Row #178's device-free test: the descriptor parser (a synthetic USB tree),
// the HID report-id reader, the UVC payload assembler and the HID output
// report builder. No device, no IOKit, no network.
#include "descriptor_probe.h"
#include "hid_report.h"
#include "raw_capture.h"
#include "uvc_frames.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace rc = raw_capture;

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++g_failures;
}

void Put16(std::vector<uint8_t>* b, uint16_t v) {
    b->push_back(static_cast<uint8_t>(v & 0xff));
    b->push_back(static_cast<uint8_t>(v >> 8));
}
void Put32(std::vector<uint8_t>* b, uint32_t v) {
    for (int i = 0; i < 4; ++i) b->push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xff));
}

// One endpoint descriptor: bLength, bDescriptorType 0x05, address, attributes,
// wMaxPacketSize, bInterval.
void AddEndpoint(std::vector<uint8_t>* b, uint8_t address, uint8_t attributes, uint16_t max_packet,
                 uint8_t interval) {
    b->push_back(7);
    b->push_back(0x05);
    b->push_back(address);
    b->push_back(attributes);
    Put16(b, max_packet);
    b->push_back(interval);
}

// A configuration descriptor with: a UVC VideoControl interface (one
// extension unit), a UVC VideoStreaming interface (GREY 640x480, one bulk
// endpoint), a HID interface (a 64-byte report descriptor, one interrupt IN
// endpoint), and a vendor interface (one bulk endpoint).
std::vector<uint8_t> SyntheticTree() {
    std::vector<uint8_t> body;

    // VideoControl interface (0, alt 0).
    const uint8_t vc[] = {9, 0x04, 0, 0, 0, 0x0E, 0x01, 0x00, 0};
    body.insert(body.end(), vc, vc + sizeof(vc));
    // VC header (class-specific, subtype 1).
    const uint8_t vc_header[] = {13, 0x24, 0x01, 0x00, 0x01, 12, 0, 0, 0, 0, 1, 1, 0};
    body.insert(body.end(), vc_header, vc_header + sizeof(vc_header));
    // Extension unit (subtype 6, unit id 7).
    const uint8_t xu[] = {24, 0x24, 0x06, 7, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    body.insert(body.end(), xu, xu + sizeof(xu));

    // VideoStreaming interface (1, alt 1), one endpoint.
    const uint8_t vs[] = {9, 0x04, 1, 1, 1, 0x0E, 0x02, 0x00, 0};
    body.insert(body.end(), vs, vs + sizeof(vs));
    // VS input header: bNumFormats 1 (offset 3), bEndpointAddress 0x81 (offset 6).
    uint8_t vs_header[14] = {14, 0x24, 0x01, 1, 0, 0, 0x81, 0, 0, 0, 0, 0, 0, 0};
    body.insert(body.end(), vs_header, vs_header + sizeof(vs_header));
    // Format descriptor: GREY (little-endian fourcc), 8 bits per pixel.
    uint8_t format[28] = {0};
    format[0] = 28;
    format[1] = 0x24;
    format[2] = 0x04;
    format[3] = 1;   // bFormatIndex
    format[4] = 1;   // bNumFrameDescriptors
    const char grey[4] = {'G', 'R', 'E', 'Y'};
    std::memcpy(format + 5, grey, 4);  // guidFormat at +5, remaining 12 bytes stay 0
    format[21] = 8;  // bBitsPerPixel
    body.insert(body.end(), format, format + sizeof(format));
    // Frame descriptor: 640x480, 307200 bytes, default interval 333333 (100ns).
    uint8_t frame[30] = {0};
    frame[0] = 30;
    frame[1] = 0x24;
    frame[2] = 0x05;
    frame[3] = 1;
    frame[5] = 640 & 0xff;
    frame[6] = 640 >> 8;
    frame[7] = 480 & 0xff;
    frame[8] = 480 >> 8;
    frame[17] = 307200 & 0xff;   // dwMaxVideoFrameBufferSize at +17
    frame[18] = (307200 >> 8) & 0xff;
    frame[19] = (307200 >> 16) & 0xff;
    frame[20] = (307200 >> 24) & 0xff;
    frame[21] = 333333 & 0xff;   // dwDefaultFrameInterval at +21
    frame[22] = (333333 >> 8) & 0xff;
    frame[23] = (333333 >> 16) & 0xff;
    frame[24] = (333333 >> 24) & 0xff;
    body.insert(body.end(), frame, frame + sizeof(frame));
    // Bulk IN endpoint 0x81, 1024-byte packets.
    AddEndpoint(&body, 0x81, 0x02, 1024, 0);

    // HID interface (2, alt 0), one interrupt IN endpoint and a HID descriptor.
    const uint8_t hid_if[] = {9, 0x04, 2, 0, 1, 0x03, 0x00, 0x00, 0};
    body.insert(body.end(), hid_if, hid_if + sizeof(hid_if));
    const uint8_t hid_desc[] = {9, 0x21, 0x11, 0x01, 0, 1, 0x22, 64, 0};
    body.insert(body.end(), hid_desc, hid_desc + sizeof(hid_desc));
    AddEndpoint(&body, 0x83, 0x03, 8, 10);

    // Vendor interface (3, alt 0), one bulk OUT endpoint.
    const uint8_t vendor_if[] = {9, 0x04, 3, 0, 1, 0xFF, 0x00, 0x00, 0};
    body.insert(body.end(), vendor_if, vendor_if + sizeof(vendor_if));
    AddEndpoint(&body, 0x02, 0x02, 512, 0);

    std::vector<uint8_t> blob;
    const size_t total = 9 + body.size();
    blob.push_back(9);
    blob.push_back(0x02);
    Put16(&blob, static_cast<uint16_t>(total));
    blob.push_back(4);   // bNumInterfaces
    blob.push_back(1);   // bConfigurationValue
    blob.push_back(0);
    blob.push_back(0x80);
    blob.push_back(50);
    blob.insert(blob.end(), body.begin(), body.end());
    return blob;
}

const rc::ProbeInterface* FindInterface(const rc::ProbeResult& result, int number) {
    for (const auto& interface : result.interfaces) {
        if (interface.number == number) return &interface;
    }
    return nullptr;
}

rc::ControlSpec ConfiguredControl() {
    rc::ControlSpec spec;
    spec.knowledge = rc::Knowledge::kKnown;
    spec.endpoint.knowledge = rc::Knowledge::kKnown;
    spec.endpoint.interface_number = 2;
    spec.endpoint.alternate_setting = 0;
    spec.endpoint.endpoint_address = 0x05;
    spec.endpoint.transfer = rc::TransferType::kInterrupt;
    spec.report_id = 5;
    spec.report_length = 12;
    spec.fields[0] = {3, 5, 2, 1, rc::FieldEncoding::kU8, true};
    spec.fields[1] = {152, 5, 3, 1, rc::FieldEncoding::kU8, true};
    spec.fields[2] = {153, 5, 4, 1, rc::FieldEncoding::kU8, true};
    spec.fields[3] = {154, 5, 5, 4, rc::FieldEncoding::kF32le, true};
    spec.fields[4] = {155, 5, 1, 4, rc::FieldEncoding::kF32le, true};
    return spec;
}

}  // namespace

int main() {
    // --- the descriptor parser -------------------------------------------
    {
        const std::vector<uint8_t> blob = SyntheticTree();
        rc::ProbeResult result;
        std::string error;
        const bool ok = rc::ParseConfigurationDescriptor(blob.data(), blob.size(), &result, &error);
        Check(ok, "the synthetic configuration descriptor parses");
        if (!ok) std::printf("  error: %s\n", error.c_str());
        Check(result.configuration_value == 1, "the configuration value is read");
        Check(result.interfaces.size() == 4, "four interfaces are read");

        const rc::ProbeInterface* vc = FindInterface(result, 0);
        Check(vc != nullptr && vc->video_control, "interface 0 is VideoControl");
        Check(vc != nullptr && vc->extension_units.size() == 1 && vc->extension_units[0] == 7,
              "the VC extension unit id is read");

        const rc::ProbeInterface* vs = FindInterface(result, 1);
        Check(vs != nullptr && vs->video_streaming, "interface 1 is VideoStreaming");
        Check(vs != nullptr && vs->streaming_endpoint_address == 0x81, "the VS endpoint address is read");
        Check(vs != nullptr && vs->format_fourcc == 0x59455247u, "the format fourcc is GREY");
        Check(vs != nullptr && vs->bits_per_pixel == 8, "the format's bits per pixel is read");
        Check(vs != nullptr && vs->width == 640 && vs->height == 480, "the frame size is read");
        Check(vs != nullptr && vs->max_frame_bytes == 307200, "the max frame bytes are read");
        Check(vs != nullptr && vs->bytes_per_line == 640, "the stride is derived (307200/480)");
        Check(vs != nullptr && vs->endpoints.size() == 1 && vs->endpoints[0].transfer == rc::TransferType::kBulk &&
                  vs->endpoints[0].max_packet_size == 1024,
              "the bulk endpoint and its packet size are read");

        const rc::ProbeInterface* hid = FindInterface(result, 2);
        Check(hid != nullptr && hid->hid, "interface 2 is HID");
        Check(hid != nullptr && hid->hid_report_descriptor_length == 64,
              "the HID report descriptor length is read");
        Check(hid != nullptr && hid->endpoints.size() == 1 &&
                  hid->endpoints[0].transfer == rc::TransferType::kInterrupt,
              "the HID interrupt endpoint is read");

        const rc::ProbeInterface* vendor = FindInterface(result, 3);
        Check(vendor != nullptr && vendor->vendor_specific, "interface 3 is vendor-specific");
        Check(vendor != nullptr && vendor->endpoints.size() == 1 &&
                  vendor->endpoints[0].address == 0x02,
              "the vendor OUT endpoint is read");

        // The copy-paste block uses the table's field names.
        const std::string printed = rc::FormatProbe(result);
        Check(printed.find("stream[0].endpoint.endpoint_address=0x81") != std::string::npos,
              "the printout names stream[0].endpoint.endpoint_address");
        Check(printed.find("stream[0].pixel_format=0x59455247") != std::string::npos,
              "the printout names stream[0].pixel_format");
        Check(printed.find("stream[0].bytes_per_line=640") != std::string::npos,
              "the printout names stream[0].bytes_per_line");
        Check(printed.find("control.report_descriptor_length=64") != std::string::npos,
              "the printout names control.report_descriptor_length");
        Check(printed.find("interface number=2 alternate_setting=0") != std::string::npos,
              "the printout lists every interface");
    }

    // --- malformed descriptors -------------------------------------------
    {
        const uint8_t not_config[] = {9, 0x04, 0, 0, 0, 0, 0, 0, 0};
        rc::ProbeResult result;
        std::string error;
        Check(!rc::ParseConfigurationDescriptor(not_config, sizeof(not_config), &result, &error),
              "a non-configuration descriptor is refused");
        Check(!result.error.empty(), "the refusal is named");
    }
    {
        std::vector<uint8_t> blob = SyntheticTree();
        blob.resize(20);  // wTotalLength still claims the full size
        rc::ProbeResult result;
        std::string error;
        Check(!rc::ParseConfigurationDescriptor(blob.data(), blob.size(), &result, &error),
              "a truncated descriptor is refused");
        Check(result.error.find("truncated") != std::string::npos, "the truncation is named");
    }
    {
        std::vector<uint8_t> blob = SyntheticTree();
        blob[18] = 1;  // the VC header's bLength -> 1
        rc::ProbeResult result;
        std::string error;
        Check(!rc::ParseConfigurationDescriptor(blob.data(), blob.size(), &result, &error),
              "a malformed bLength is refused");
    }

    // --- HID report ids ----------------------------------------------------
    {
        const uint8_t report_descriptor[] = {
            0x06, 0x00, 0xFF,  // usage page
            0x09, 0x01,        // usage
            0xA1, 0x01,        // collection
            0x85, 0x05,        // report id 5
            0x09, 0x01, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x08, 0x81, 0x02,
            0x85, 0x06,        // report id 6
            0x09, 0x02, 0x75, 0x08, 0x95, 0x08, 0x91, 0x02,
            0xC0,
        };
        std::string error;
        const std::vector<int> ids =
            rc::ParseHidReportIds(report_descriptor, sizeof(report_descriptor), &error);
        Check(ids.size() == 2 && ids[0] == 5 && ids[1] == 6, "the HID report ids are read");
        Check(error.empty(), "a valid report descriptor has no error");
    }

    // --- the UVC payload assembler ----------------------------------------
    {
        rc::UvcAssembler assembler(4);
        rc::UvcFrame frame;
        std::string error;
        const uint8_t part[] = {2, 0x00, 0xAA, 0xBB};  // FID 0, no EOF
        Check(!assembler.Feed(part, sizeof(part), &frame, &error), "a partial payload makes no frame");
        const uint8_t end[] = {2, 0x02, 0xCC, 0xDD};  // FID 0, EOF
        Check(assembler.Feed(end, sizeof(end), &frame, &error), "EOF completes the frame");
        Check(frame.data.size() == 4 && frame.data[0] == 0xAA && frame.data[3] == 0xDD,
              "the assembled frame carries both payloads");
        Check(frame.fid == 0, "the frame's FID is carried");
    }
    {
        rc::UvcAssembler assembler(16);
        rc::UvcFrame frame;
        std::string error;
        const uint8_t first[] = {2, 0x00, 1, 2};      // FID 0
        const uint8_t changed[] = {2, 0x01, 3, 4};    // FID 1 before EOF
        const uint8_t end[] = {2, 0x03, 5, 6};        // FID 1, EOF
        assembler.Feed(first, sizeof(first), &frame, &error);
        Check(!assembler.Feed(changed, sizeof(changed), &frame, &error), "a FID change drops the partial frame");
        Check(assembler.frames_dropped() == 1, "the drop is counted");
        Check(assembler.Feed(end, sizeof(end), &frame, &error), "the new frame completes");
        Check(frame.data.size() == 4 && frame.data[0] == 3 && frame.fid == 1, "the new frame is intact");
    }
    {
        rc::UvcAssembler assembler(16);
        rc::UvcFrame frame;
        std::string error;
        const uint8_t damaged[] = {2, 0x40, 1, 2};  // ERR bit
        Check(!assembler.Feed(damaged, sizeof(damaged), &frame, &error), "the error bit drops the frame");
        Check(assembler.frames_dropped() == 1, "the error drop is counted");
    }
    {
        rc::UvcAssembler assembler(4);
        rc::UvcFrame frame;
        std::string error;
        const uint8_t too_big[] = {2, 0x00, 1, 2, 3, 4, 5};  // 5 bytes, no EOF
        Check(!assembler.Feed(too_big, sizeof(too_big), &frame, &error), "an oversized frame is refused");
        Check(error.find("maximum frame size") != std::string::npos, "the overflow is named");
    }

    // --- the HID report builder -------------------------------------------
    {
        const rc::ControlSpec spec = ConfiguredControl();
        std::vector<uint8_t> report;
        std::string error;
        Check(rc::BuildHidReport(spec, 3, 1.0, &report, &error), "property 3 builds a report");
        Check(report.size() == 12 && report[0] == 5, "the report carries its report id");
        Check(report[2] == 1, "the u8 field is written at its offset");

        Check(rc::BuildHidReport(spec, 154, 500000.0, &report, &error), "property 154 builds a report");
        float value = 0.0f;
        std::memcpy(&value, report.data() + 5, sizeof(value));
        Check(value == 500000.0f, "the f32 field is written at its offset");

        Check(!rc::BuildHidReport(spec, 99, 1.0, &report, &error), "an unknown property is refused");
        Check(error.find("not-configured") == 0, "the unknown property refusal is named");

        rc::ControlSpec partial = spec;
        partial.fields[2].configured = false;
        Check(!rc::BuildHidReport(partial, 153, 1.0, &report, &error),
              "an unconfigured field is refused");
        Check(!rc::ControlFieldConfigured(partial, 153), "the unconfigured field reports not-configured");

        Check(!rc::BuildHidReport(spec, 3, 300.0, &report, &error), "a u8 overflow is refused");
        Check(error.find("value-out-of-range") == 0, "the overflow is named");

        rc::ControlSpec empty;
        Check(!rc::BuildHidReport(empty, 3, 1.0, &report, &error), "an unconfigured table is refused");
        Check(error.find("not-configured") == 0, "the unconfigured table is named");
    }

    std::printf("[raw-capture-probe] %d check(s), %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
