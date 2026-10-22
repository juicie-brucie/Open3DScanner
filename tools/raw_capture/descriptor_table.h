// The one table of USB descriptor / interface / endpoint constants the raw
// capture and the vendor-command path use (rows #171, #178, #182, #186).
//
// Filled from the daemon-stopped probe (tests/results/2026-10-21-plan178-probe.md,
// raw log beside it) and the #182 interface-name mapping. Nothing here is
// guessed: every entry is either kKnown (with its evidence named beside it) or
// kUnknown, and the code refuses to run against an unknown entry rather than
// inventing one. The control report id / field map is the one part still
// unknown — it needs the HID report descriptor, which the probe could not
// fetch; the code refuses it by name until it is captured.
#pragma once

#include <cstdint>

namespace raw_capture {

enum class Knowledge { kKnown, kUnknown };

enum class TransferType { kUnknown, kControl, kBulk, kInterrupt, kIsochronous };

enum class StreamKind { kIr = 0, kDepth = 1, kRgb = 2 };
inline constexpr int kStreamCount = 3;

constexpr int kUnset = -1;

struct EndpointSpec {
    Knowledge knowledge = Knowledge::kUnknown;
    int interface_number = kUnset;      // bInterfaceNumber
    int alternate_setting = kUnset;     // bAlternateSetting carrying the endpoint
    int endpoint_address = kUnset;      // bEndpointAddress (0x80 = IN)
    TransferType transfer = TransferType::kUnknown;
    int max_packet_size = 0;            // wMaxPacketSize
};

struct StreamSpec {
    StreamKind kind = StreamKind::kIr;
    const char* name = "";
    const char* rpc_suffix = "";        // the logical method suffix (protocol-spec.md §1.1)
    uint16_t rpc_port = 0;              // the logical RPC port for the same interface
    Knowledge rpc_port_knowledge = Knowledge::kUnknown;
    EndpointSpec endpoint;              // the USB side: unknown until the capture
    uint32_t pixel_format = 0;          // UVC format GUID / fourcc; 0 = unknown
    uint32_t bytes_per_line = 0;        // stride; 0 = unknown
    uint32_t width = 0;                 // 0 = unknown
    uint32_t height = 0;                // 0 = unknown
};

// The vendor-command transport (laser/exposure, #101's property map).
//
// The endpoint is one field; the report layout is another. The descriptor
// probe finds the endpoint and the HID descriptor's length, but the mapping
// from #101's property ids to report bytes needs the HID report descriptor's
// usages, which #169 has not captured. Every field below is therefore
// unconfigured until the operator fills it from that capture.
enum class FieldEncoding { kUnknown, kU8, kU16le, kU32le, kF32le };

struct PropertyField {
    uint32_t property = 0;
    int report_id = kUnset;
    int offset = kUnset;                // byte offset in the output report
    int size = 0;                       // 1, 2 or 4 bytes
    FieldEncoding encoding = FieldEncoding::kUnknown;
    bool configured = false;
};

struct ControlSpec {
    Knowledge knowledge = Knowledge::kUnknown;
    const char* missing = "the HID report id and the property->report field map are not captured";
    EndpointSpec endpoint;              // known: the IMU HID interface (probe, row #178)
    int report_descriptor_length = 0;   // the HID descriptor's report length (25, observed)
    int report_id = kUnset;             // still unknown: the report descriptor was not read
    int report_length = 0;              // the output report's length, from the report descriptor
    // The #101 properties this path can carry; each is unconfigured by
    // default and is filled from the captured report descriptor.
    PropertyField fields[5];
};

struct DescriptorTable {
    // Identity: known (docs/recon/findings/usb-descriptor-map.md §1.1).
    uint16_t vendor_id = 0x2BC5;        // Orbbec
    uint16_t product_id = 0x0C03;       // Creality Raptor Pro
    Knowledge identity_knowledge = Knowledge::kKnown;

    StreamSpec ir;
    StreamSpec depth;
    StreamSpec rgb;
    // The logical interfaces that are known but not captured by this tool.
    uint16_t rgb_rpc_port = 10012;
    Knowledge rgb_rpc_port_knowledge = Knowledge::kKnown;
    uint16_t imu_rpc_port = 10011;
    Knowledge imu_rpc_port_knowledge = Knowledge::kKnown;

    ControlSpec control;

    const StreamSpec& stream(StreamKind kind) const {
        if (kind == StreamKind::kIr) return ir;
        if (kind == StreamKind::kDepth) return depth;
        return rgb;
    }
};

// The compiled-in table, filled for IR/depth/RGB/HID from the probe record
// (2026-10-21-plan178-probe.raw.log) and the #182 name mapping: IR is the
// interface named "… IR Camera" (3), depth the "… Depth Camera" (1), RGB the
// "… RGB Camera" (5), and the HID interface (6) is the "… IMU".
inline const DescriptorTable& Table() {
    static const DescriptorTable table = [] {
        DescriptorTable t;
        // IR: interface 3, endpoint 0x82, Y8 960x600, stride 960.
        t.ir.kind = StreamKind::kIr;
        t.ir.name = "ir";
        t.ir.rpc_suffix = "0-2-12";     // protocol-spec.md §1.1 (interface 2)
        t.ir.rpc_port = 10013;
        t.ir.rpc_port_knowledge = Knowledge::kKnown;
        t.ir.endpoint = {Knowledge::kKnown, 3, 0, 0x82, TransferType::kBulk, 1024};
        t.ir.pixel_format = 0x20203859;  // 'Y8  ' (UVC Y8 GUID 59382030-...)
        t.ir.bytes_per_line = 960;
        t.ir.width = 960;
        t.ir.height = 600;

        // Depth: interface 1, endpoint 0x81, Y8 480x300, stride 840.
        t.depth.kind = StreamKind::kDepth;
        t.depth.name = "depth";
        t.depth.rpc_suffix = "0-2-10";  // protocol-spec.md §1.1 (interface 0)
        t.depth.rpc_port = 10010;
        t.depth.rpc_port_knowledge = Knowledge::kKnown;
        t.depth.endpoint = {Knowledge::kKnown, 1, 0, 0x81, TransferType::kBulk, 1024};
        t.depth.pixel_format = 0x20203859;
        t.depth.bytes_per_line = 840;
        t.depth.width = 480;
        t.depth.height = 300;

        // RGB: interface 5, endpoint 0x83, YUY2 1280x800, stride 2560.
        t.rgb.kind = StreamKind::kRgb;
        t.rgb.name = "rgb";
        t.rgb.rpc_suffix = "0-2-14";    // the observed 0-2-<interface index> pattern
        t.rgb.rpc_port = 10012;
        t.rgb.rpc_port_knowledge = Knowledge::kKnown;
        t.rgb.endpoint = {Knowledge::kKnown, 5, 0, 0x83, TransferType::kBulk, 1024};
        t.rgb.pixel_format = 0x32595559;  // 'YUY2'
        t.rgb.bytes_per_line = 2560;
        t.rgb.width = 1280;
        t.rgb.height = 800;

        // HID/IMU: interface 6, endpoint 0x85, interrupt, 248-byte packets, a
        // 25-byte report descriptor. The report id and the property field map
        // are still unknown (the probe could not fetch the descriptor).
        t.control.endpoint = {Knowledge::kKnown, 6, 0, 0x85, TransferType::kInterrupt, 248};
        t.control.report_descriptor_length = 25;
        return t;
    }();
    return table;
}

// True when the stream's USB endpoint, format and stride are all known.
// Until then the UVC source refuses with the missing field named.
inline bool StreamConfigured(const StreamSpec& spec) {
    return spec.endpoint.knowledge == Knowledge::kKnown &&
           spec.endpoint.interface_number != kUnset && spec.endpoint.alternate_setting != kUnset &&
           spec.endpoint.endpoint_address != kUnset &&
           spec.endpoint.transfer != TransferType::kUnknown && spec.pixel_format != 0 &&
           spec.bytes_per_line != 0 && spec.width != 0 && spec.height != 0;
}

inline bool ControlConfigured(const ControlSpec& spec) {
    return spec.knowledge == Knowledge::kKnown && spec.endpoint.knowledge == Knowledge::kKnown &&
           spec.endpoint.interface_number != kUnset && spec.endpoint.endpoint_address != kUnset;
}

inline const char* KnowledgeName(Knowledge knowledge) {
    return knowledge == Knowledge::kKnown ? "known" : "unknown";
}

inline const char* TransferTypeName(TransferType type) {
    switch (type) {
        case TransferType::kControl: return "control";
        case TransferType::kBulk: return "bulk";
        case TransferType::kInterrupt: return "interrupt";
        case TransferType::kIsochronous: return "isochronous";
        default: return "unknown";
    }
}

}  // namespace raw_capture
