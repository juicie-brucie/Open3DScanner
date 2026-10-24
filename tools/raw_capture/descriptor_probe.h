// Row #178: parse a USB configuration descriptor (and, when available, a HID
// report descriptor) into the fields of descriptor_table.h — without guessing.
//
// The parser is pure and device-free: the macOS probe hands it the descriptor
// bytes that IOUSBHost reports, and the test hands it a synthetic tree. It
// never invents a value: a field the descriptors do not carry stays unset, and
// the printout says so.
//
// Descriptor layout references: USB 2.0 §9.6 (configuration/interface/endpoint),
// UVC 1.1 §3.7-§3.9 (VC/VS class-specific descriptors), HID 1.11 §6.2.1.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "descriptor_table.h"

namespace raw_capture {

struct ProbeEndpoint {
    int address = kUnset;               // bEndpointAddress
    TransferType transfer = TransferType::kUnknown;
    int max_packet_size = 0;            // wMaxPacketSize
    int interval = 0;                   // bInterval
};

struct ProbeInterface {
    int number = kUnset;                // bInterfaceNumber
    int alternate_setting = kUnset;     // bAlternateSetting
    int class_code = 0;
    int subclass = 0;
    int protocol = 0;
    std::vector<ProbeEndpoint> endpoints;

    bool video_control = false;         // class 0x0E / subclass 0x01
    bool video_streaming = false;       // class 0x0E / subclass 0x02
    bool hid = false;                   // class 0x03
    bool vendor_specific = false;       // class 0xFF
    std::vector<int> extension_units;   // UVC XU ids, when declared

    // VideoControl details (from the VC_HEADER class-specific descriptor,
    // UVC 1.1 §3.7.2). Determines the Video Probe/Commit Control struct's
    // length (26 B for UVC 1.0, 34 B for 1.1, 48 B for 1.5) — needed before
    // any probe/commit control transfer is attempted, never assumed.
    int bcd_uvc = kUnset;                      // bcdUVC, e.g. 0x0110 = UVC 1.10

    // VideoStreaming details (from the class-specific descriptors).
    int streaming_endpoint_address = kUnset;  // from the VS input header
    uint32_t format_guid[4] = {0, 0, 0, 0};    // 16-byte GUID, as four LE words
    uint32_t format_fourcc = 0;                // the GUID's first four bytes, when printable
    int bits_per_pixel = 0;
    int format_index = kUnset;                 // bFormatIndex (VS_FORMAT_UNCOMPRESSED +3)
    int frame_index = kUnset;                  // bFrameIndex (VS_FRAME_UNCOMPRESSED +3)
    int width = 0;
    int height = 0;
    uint32_t max_frame_bytes = 0;              // dwMaxVideoFrameBufferSize
    uint32_t bytes_per_line = 0;               // derived: max_frame_bytes / height
    int frame_interval_100ns = 0;              // dwDefaultFrameInterval

    // HID details (from the HID descriptor).
    int hid_report_descriptor_length = 0;
};

struct ProbeResult {
    int vendor_id = 0;
    int product_id = 0;
    int configuration_value = kUnset;
    std::vector<ProbeInterface> interfaces;
    // Filled when a HID report descriptor is supplied (the probe fetches it
    // when it can; the test synthesises it). Row #189: the bytes and the named
    // fetch failure are kept apart from the parsed ids.
    std::vector<int> hid_report_ids;
    std::vector<uint8_t> hid_report;      // the report descriptor bytes
    std::string hid_report_error;         // why they are absent, when they are

    bool ok() const { return error.empty(); }
    std::string error;
};

// Parses one configuration descriptor blob. Returns false with a named error
// on a malformed/truncated descriptor.
bool ParseConfigurationDescriptor(const uint8_t* data, size_t size, ProbeResult* out, std::string* error);

// The report ids a HID report descriptor declares (0x85, Report ID), in order.
std::vector<int> ParseHidReportIds(const uint8_t* data, size_t size, std::string* error);

// The configuration-descriptor length (wTotalLength), for a caller that has
// only the first 9 bytes; 0 when the buffer is too short.
size_t ConfigurationDescriptorLength(const uint8_t* data, size_t size);

// Human and copy-paste output, using descriptor_table.h's field names.
std::string FormatProbe(const ProbeResult& result);
void PrintProbe(const ProbeResult& result, std::FILE* out);

// Reads the attached scanner's descriptors (macOS, IOUSBHost) into `result`.
// Read-only: no seize, no configuration change, no interface opened. On a
// non-macOS build, or when the device cannot be opened, returns false with a
// named reason in result.error.
bool ProbeAttachedDevice(ProbeResult* result);

}  // namespace raw_capture
