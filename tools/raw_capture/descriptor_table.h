// Row #171 (Phase D seed 3): the one table of USB descriptor / interface /
// endpoint constants the raw capture and the vendor-command path use.
//
// The descriptor-level map is pending — docs/recon/findings/usb-descriptor-map.md
// records what is known (identity, the logical RPC ports) and what needs the
// operator's USB capture (interfaces, endpoints, transfer types, UVC extension
// units, HID report descriptors). Nothing here is guessed: every entry is
// either kKnown (and the finding that states it is named beside it) or
// kUnknown, and the code refuses to run against an unknown entry rather than
// inventing one. The operator's capture fills the kUnknown fields in this
// table and nowhere else.
#pragma once

#include <cstdint>

namespace raw_capture {

enum class Knowledge { kKnown, kUnknown };

enum class TransferType { kUnknown, kControl, kBulk, kInterrupt, kIsochronous };

enum class StreamKind { kIr = 0, kDepth = 1 };
inline constexpr int kStreamCount = 2;

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
struct ControlSpec {
    Knowledge knowledge = Knowledge::kUnknown;
    const char* missing = "HID report descriptor and vendor control endpoint are not in the map";
    EndpointSpec endpoint;
    int report_id = kUnset;             // HID feature report id, when it is HID
};

struct DescriptorTable {
    // Identity: known (docs/recon/findings/usb-descriptor-map.md §1.1).
    uint16_t vendor_id = 0x2BC5;        // Orbbec
    uint16_t product_id = 0x0C03;       // Creality Raptor Pro
    Knowledge identity_knowledge = Knowledge::kKnown;

    StreamSpec ir;
    StreamSpec depth;
    // The logical interfaces that are known but not captured by this tool.
    uint16_t rgb_rpc_port = 10012;
    Knowledge rgb_rpc_port_knowledge = Knowledge::kKnown;
    uint16_t imu_rpc_port = 10011;
    Knowledge imu_rpc_port_knowledge = Knowledge::kKnown;

    ControlSpec control;

    const StreamSpec& stream(StreamKind kind) const { return kind == StreamKind::kIr ? ir : depth; }
};

// The compiled-in table. Identity and the logical RPC ports are the known
// half (usb-descriptor-map.md §1.1-§1.3); every USB endpoint, UVC format and
// HID/vendor control field is kUnknown until the operator's capture lands.
inline const DescriptorTable& Table() {
    static const DescriptorTable table = [] {
        DescriptorTable t;
        t.ir.kind = StreamKind::kIr;
        t.ir.name = "ir";
        t.ir.rpc_suffix = "0-2-12";     // protocol-spec.md §1.1 (interface 2)
        t.ir.rpc_port = 10013;
        t.ir.rpc_port_knowledge = Knowledge::kKnown;

        t.depth.kind = StreamKind::kDepth;
        t.depth.name = "depth";
        t.depth.rpc_suffix = "0-2-10";  // protocol-spec.md §1.1 (interface 0)
        t.depth.rpc_port = 10010;
        t.depth.rpc_port_knowledge = Knowledge::kKnown;
        return t;
    }();
    return table;
}

// True when the stream's USB endpoint, format and stride are all known.
// Until then the UVC source refuses with the missing field named.
inline bool StreamConfigured(const StreamSpec& spec) {
    return spec.endpoint.knowledge == Knowledge::kKnown &&
           spec.endpoint.interface_number != kUnset && spec.endpoint.endpoint_address != kUnset &&
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
