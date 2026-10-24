// The descriptor parser behind `raw_capture --probe` (row #178). See
// descriptor_probe.h for the contract: it fills only what the bytes carry.

#include "descriptor_probe.h"

#include <algorithm>
#include <cstdarg>
#include <cstring>

namespace raw_capture {
namespace {

constexpr uint8_t kDescriptorConfiguration = 0x02;
constexpr uint8_t kDescriptorInterface = 0x04;
constexpr uint8_t kDescriptorEndpoint = 0x05;
constexpr uint8_t kDescriptorHid = 0x21;
constexpr uint8_t kDescriptorHidReport = 0x22;
constexpr uint8_t kDescriptorCsInterface = 0x24;

constexpr int kClassVideo = 0x0E;
constexpr int kSubclassVideoControl = 0x01;
constexpr int kSubclassVideoStreaming = 0x02;
constexpr int kClassHid = 0x03;
constexpr int kClassVendor = 0xFF;

uint16_t U16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t U32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

TransferType TransferFromAttributes(uint8_t attributes) {
    switch (attributes & 0x03) {
        case 0: return TransferType::kControl;
        case 1: return TransferType::kIsochronous;
        case 2: return TransferType::kBulk;
        case 3: return TransferType::kInterrupt;
        default: return TransferType::kUnknown;
    }
}

bool PrintableFourcc(uint32_t fourcc) {
    for (int i = 0; i < 4; ++i) {
        const unsigned char c = static_cast<unsigned char>((fourcc >> (i * 8)) & 0xff);
        if (c < 0x20 || c > 0x7e) return false;
    }
    return true;
}

void Append(std::string* text, const char* format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    const int written = std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    if (written > 0) {
        text->append(buffer, std::min<size_t>(static_cast<size_t>(written), sizeof(buffer) - 1));
    }
}

std::string FourccText(uint32_t fourcc) {
    std::string text;
    for (int i = 0; i < 4; ++i) text.push_back(static_cast<char>((fourcc >> (i * 8)) & 0xff));
    return text;
}

}  // namespace

size_t ConfigurationDescriptorLength(const uint8_t* data, size_t size) {
    if (data == nullptr || size < 4 || data[1] != kDescriptorConfiguration) return 0;
    return U16(data + 2);
}

bool ParseConfigurationDescriptor(const uint8_t* data, size_t size, ProbeResult* out, std::string* error) {
    out->interfaces.clear();
    out->hid_report_ids.clear();
    out->error.clear();
    if (data == nullptr || size < 9 || data[1] != kDescriptorConfiguration) {
        out->error = "not a configuration descriptor (bDescriptorType != 0x02)";
        *error = out->error;
        return false;
    }
    const uint16_t total = U16(data + 2);
    if (total < 9 || total > size) {
        out->error = "truncated configuration descriptor (wTotalLength " + std::to_string(total) +
                     " > " + std::to_string(size) + " bytes)";
        *error = out->error;
        return false;
    }
    out->configuration_value = data[5] == 0 ? kUnset : data[5];

    ProbeInterface* current = nullptr;
    // The format a VS_FRAME descriptor belongs to is whichever VS_FORMAT
    // descriptor most recently preceded it (UVC nests each format's frames
    // directly after it) — never the interface's last format. Reset on every
    // new interface so a later interface cannot inherit an earlier one's value.
    int pending_format_index = kUnset;
    size_t at = 0;
    while (at + 2 <= total) {
        const uint8_t length = data[at];
        const uint8_t type = data[at + 1];
        if (length < 2 || at + length > total) {
            out->error = "malformed descriptor at offset " + std::to_string(at) + " (bLength " +
                         std::to_string(length) + ")";
            *error = out->error;
            return false;
        }
        if (type == kDescriptorInterface) {
            if (length < 9) {
                out->error = "short interface descriptor at offset " + std::to_string(at);
                *error = out->error;
                return false;
            }
            pending_format_index = kUnset;
            ProbeInterface interface;
            interface.number = data[at + 2];
            interface.alternate_setting = data[at + 3];
            interface.class_code = data[at + 5];
            interface.subclass = data[at + 6];
            interface.protocol = data[at + 7];
            interface.video_control = interface.class_code == kClassVideo &&
                                      interface.subclass == kSubclassVideoControl;
            interface.video_streaming = interface.class_code == kClassVideo &&
                                        interface.subclass == kSubclassVideoStreaming;
            interface.hid = interface.class_code == kClassHid;
            interface.vendor_specific = interface.class_code == kClassVendor;
            out->interfaces.push_back(interface);
            current = &out->interfaces.back();
        } else if (type == kDescriptorEndpoint) {
            if (length < 7) {
                out->error = "short endpoint descriptor at offset " + std::to_string(at);
                *error = out->error;
                return false;
            }
            if (current == nullptr) {
                out->error = "endpoint descriptor before any interface at offset " + std::to_string(at);
                *error = out->error;
                return false;
            }
            ProbeEndpoint endpoint;
            endpoint.address = data[at + 2];
            endpoint.transfer = TransferFromAttributes(data[at + 3]);
            endpoint.max_packet_size = U16(data + at + 4) & 0x07FF;  // wMaxPacketSize bits 0-10
            endpoint.interval = data[at + 6];
            current->endpoints.push_back(endpoint);
        } else if (type == kDescriptorHid && current != nullptr && length >= 9) {
            // HID 1.11 §6.2.1: bNumDescriptors at +5, then (bDescriptorType, wDescriptorLength) pairs.
            const int count = data[at + 5];
            for (int i = 0; i < count; ++i) {
                const size_t entry = at + 6 + static_cast<size_t>(i) * 3;
                if (entry + 3 > at + length) break;
                if (data[entry] == kDescriptorHidReport) {
                    current->hid_report_descriptor_length = U16(data + entry + 1);
                }
            }
        } else if (type == kDescriptorCsInterface && current != nullptr && length >= 3) {
            const int subtype = data[at + 2];
            if (current->video_control && subtype == 0x01 && length >= 5) {
                // VC_HEADER (UVC 1.1 §3.7.2): bcdUVC at +3 (2 bytes, BCD —
                // 0x0100/0x0110/0x0150 for UVC 1.0/1.1/1.5). Determines the
                // Video Probe/Commit Control struct length; read, never assumed.
                current->bcd_uvc = static_cast<int>(U16(data + at + 3));
            } else if (current->video_control && subtype == 0x06 && length >= 4) {
                current->extension_units.push_back(data[at + 3]);
            } else if (current->video_streaming) {
                if (subtype == 0x01 && length >= 7) {
                    // VS input header (UVC 1.1 §3.9.2.2): bNumFormats at +3,
                    // wTotalLength +4, bEndpointAddress at +6.
                    current->streaming_endpoint_address = data[at + 6];
                } else if (subtype == 0x04 && length >= 22) {
                    // VS format uncompressed (UVC 1.1 §3.9.2.4): bFormatIndex
                    // at +3, the 16-byte guidFormat at +5, bBitsPerPixel at
                    // +21. A compressed format (e.g. MJPEG, 11 bytes) carries
                    // no GUID; its format stays unknown rather than misread.
                    // Tracked as "pending", not written to current->format_index
                    // directly — an interface can declare several formats, and
                    // the one that matters is whichever one's frame we keep
                    // below, never just the last format descriptor seen.
                    pending_format_index = data[at + 3];
                    for (int i = 0; i < 4; ++i) current->format_guid[i] = U32(data + at + 5 + i * 4);
                    const uint32_t fourcc = U32(data + at + 5);
                    current->format_fourcc = PrintableFourcc(fourcc) ? fourcc : 0;
                    current->bits_per_pixel = data[at + 21];
                } else if (subtype == 0x05 && length >= 26) {
                    // VS frame (UVC 1.1 §3.9.2.5): bFrameIndex at +3,
                    // wWidth +5, wHeight +7, dwMaxVideoFrameBufferSize +17,
                    // dwDefaultFrameInterval +21.
                    const int frame_index = data[at + 3];
                    const int width = U16(data + at + 5);
                    const int height = U16(data + at + 7);
                    const uint32_t frame_bytes = U32(data + at + 17);
                    // Keep the first frame, paired with the format descriptor
                    // that immediately preceded it (UVC nests each format's
                    // frames directly after it) — never a later format's
                    // index. A more detailed probe could pick the format's
                    // bDefaultFrameIndex instead of the first frame seen.
                    if (current->width == 0 && current->height == 0) {
                        current->format_index = pending_format_index;
                        current->frame_index = frame_index;
                        current->width = width;
                        current->height = height;
                        current->max_frame_bytes = frame_bytes;
                        current->frame_interval_100ns = static_cast<int>(U32(data + at + 21));
                        if (height > 0) current->bytes_per_line = frame_bytes / static_cast<uint32_t>(height);
                    }
                }
            }
        }
        at += length;
    }
    if (out->interfaces.empty()) {
        out->error = "the configuration descriptor declares no interfaces";
        *error = out->error;
        return false;
    }
    return true;
}

std::vector<int> ParseHidReportIds(const uint8_t* data, size_t size, std::string* error) {
    std::vector<int> ids;
    error->clear();
    if (data == nullptr) {
        *error = "no report descriptor bytes";
        return ids;
    }
    // Short items: prefix 0b10111101 (0x85) with a 1-byte report id.
    size_t at = 0;
    while (at < size) {
        const uint8_t prefix = data[at++];
        if (prefix == 0xFE) {  // long item: bDataSize, bLongItemTag, data
            if (at + 2 > size) {
                *error = "truncated long item";
                return ids;
            }
            const size_t data_size = data[at];
            at += 2 + data_size;
            continue;
        }
        const size_t item_size = prefix & 0x03;
        const size_t bytes = item_size == 3 ? 4 : item_size;
        if (at + bytes > size) {
            *error = "truncated report item";
            return ids;
        }
        if ((prefix & 0xFC) == 0x84) {  // 0x85 with size 1
            if (bytes == 1) ids.push_back(data[at]);
        }
        at += bytes;
    }
    return ids;
}

std::string FormatProbe(const ProbeResult& result) {
    std::string text;
    Append(&text, "[probe] vid=0x%04X pid=0x%04X configuration=%d interfaces=%zu\n", result.vendor_id,
                 result.product_id, result.configuration_value, result.interfaces.size());
    // Row #197: every alternate setting per interface, so the streaming alt
    // (the one with the endpoint) is visible even when alt 0 has none.
    std::vector<int> interface_numbers;
    for (const ProbeInterface& interface : result.interfaces) {
        if (std::find(interface_numbers.begin(), interface_numbers.end(), interface.number) ==
            interface_numbers.end()) {
            interface_numbers.push_back(interface.number);
        }
    }
    for (int number : interface_numbers) {
        int count = 0;
        for (const ProbeInterface& interface : result.interfaces) {
            if (interface.number == number) ++count;
        }
        Append(&text, "[probe] interface %d alternates: %d", number, count);
        for (const ProbeInterface& interface : result.interfaces) {
            if (interface.number != number) continue;
            Append(&text, " [alt %d:", interface.alternate_setting);
            if (interface.endpoints.empty()) {
                Append(&text, " no endpoints");
            } else {
                for (const ProbeEndpoint& endpoint : interface.endpoints) {
                    Append(&text, " 0x%02X %s %d", endpoint.address,
                           TransferTypeName(endpoint.transfer), endpoint.max_packet_size);
                }
            }
            Append(&text, "]");
        }
        Append(&text, "\n");
    }
    int streaming_index = 0;
    int control_printed = 0;
    for (const ProbeInterface& interface : result.interfaces) {
        const char* kind = interface.video_control ? "video-control"
                           : interface.video_streaming ? "video-streaming"
                           : interface.hid ? "hid"
                           : interface.vendor_specific ? "vendor"
                                                       : "other";
        Append(&text, "[probe] interface number=%d alternate_setting=%d class=0x%02X subclass=0x%02X "
                          "protocol=0x%02X kind=%s\n",
                     interface.number, interface.alternate_setting, interface.class_code, interface.subclass,
                     interface.protocol, kind);
        for (const ProbeEndpoint& endpoint : interface.endpoints) {
            Append(&text,
                         "[probe] endpoint interface=%d alternate_setting=%d address=0x%02X transfer=%s "
                         "max_packet_size=%d interval=%d\n",
                         interface.number, interface.alternate_setting, endpoint.address,
                         TransferTypeName(endpoint.transfer), endpoint.max_packet_size, endpoint.interval);
        }
        if (!interface.extension_units.empty()) {
            Append(&text, "[probe] extension_units=");
            for (size_t i = 0; i < interface.extension_units.size(); ++i) {
                Append(&text, "%s%d", i ? "," : "", interface.extension_units[i]);
            }
            Append(&text, "\n");
        }
        if (interface.video_control) {
            Append(&text, "[probe] video_control bcd_uvc=");
            if (interface.bcd_uvc == kUnset) {
                Append(&text, "unknown\n");
            } else {
                // UVC 1.0/1.1/1.5 fix the Video Probe/Commit Control struct
                // length at 26/34/48 bytes respectively (Table 4-47/4-76/4-75
                // across the three spec revisions) — named here so the
                // control-transfer code is built from evidence, not a guess.
                const int probe_commit_len = interface.bcd_uvc >= 0x0150 ? 48
                                            : interface.bcd_uvc >= 0x0110 ? 34
                                                                          : 26;
                Append(&text, "0x%04X (probe_commit_control_length=%d)\n", interface.bcd_uvc,
                             probe_commit_len);
            }
        }
        if (interface.video_streaming) {
            Append(&text, "[probe] video_streaming format_index=%s frame_index=%s\n",
                         interface.format_index == kUnset ? "unknown" : std::to_string(interface.format_index).c_str(),
                         interface.frame_index == kUnset ? "unknown" : std::to_string(interface.frame_index).c_str());
            if (interface.format_fourcc != 0) {
                Append(&text, "[probe] video_streaming format_guid=0x%08X%08X%08X%08X fourcc=%s "
                                  "bits_per_pixel=%d width=%d height=%d max_frame_bytes=%u "
                                  "bytes_per_line=%u (derived) frame_interval_100ns=%d\n",
                             interface.format_guid[3], interface.format_guid[2], interface.format_guid[1],
                             interface.format_guid[0], FourccText(interface.format_fourcc).c_str(),
                             interface.bits_per_pixel, interface.width, interface.height,
                             interface.max_frame_bytes, interface.bytes_per_line,
                             interface.frame_interval_100ns);
            } else {
                Append(&text, "[probe] video_streaming format=unknown width=%d height=%d "
                                  "max_frame_bytes=%u bytes_per_line=%u (derived)\n",
                             interface.width, interface.height, interface.max_frame_bytes,
                             interface.bytes_per_line);
            }
            Append(&text, "[probe] video_streaming streaming_endpoint_address=");
            if (interface.streaming_endpoint_address == kUnset) {
                Append(&text, "unknown\n");
            } else {
                Append(&text, "0x%02X\n", interface.streaming_endpoint_address);
            }

            // The copy-paste block. The IR/depth assignment is the operator's:
            // the RPC->endpoint map (OQ-169e) is what says which streaming
            // interface is which. An alternate with no endpoint (the usual
            // UVC alt 0) gets no block: the streaming alt is the one with it.
            const int declared_address = interface.streaming_endpoint_address != kUnset
                                             ? interface.streaming_endpoint_address
                                             : (interface.endpoints.empty()
                                                    ? kUnset
                                                    : interface.endpoints[0].address);
            if (declared_address == kUnset) {
                Append(&text, "[probe] note: interface %d alt %d carries no endpoint; the streaming "
                              "alternate is a different alt\n",
                       interface.number, interface.alternate_setting);
                continue;
            }
            Append(&text, "[probe] stream[%d].endpoint.interface_number=%d\n", streaming_index,
                         interface.number);
            Append(&text, "[probe] stream[%d].endpoint.alternate_setting=%d\n", streaming_index,
                         interface.alternate_setting);
            const int address = interface.streaming_endpoint_address != kUnset
                                    ? interface.streaming_endpoint_address
                                    : (interface.endpoints.empty() ? kUnset
                                                                   : interface.endpoints[0].address);
            if (address == kUnset) {
                Append(&text, "[probe] stream[%d].endpoint.endpoint_address=unknown\n", streaming_index);
            } else {
                Append(&text, "[probe] stream[%d].endpoint.endpoint_address=0x%02X\n", streaming_index,
                             address);
            }
            TransferType transfer = TransferType::kUnknown;
            int max_packet = 0;
            for (const ProbeEndpoint& endpoint : interface.endpoints) {
                if (endpoint.address == address) {
                    transfer = endpoint.transfer;
                    max_packet = endpoint.max_packet_size;
                }
            }
            Append(&text, "[probe] stream[%d].endpoint.transfer=%s\n", streaming_index,
                         TransferTypeName(transfer));
            Append(&text, "[probe] stream[%d].endpoint.max_packet_size=%d\n", streaming_index, max_packet);
            if (interface.format_fourcc != 0) {
                Append(&text, "[probe] stream[%d].pixel_format=0x%08X\n", streaming_index,
                             interface.format_fourcc);
            } else {
                Append(&text, "[probe] stream[%d].pixel_format=unknown\n", streaming_index);
            }
            Append(&text, "[probe] stream[%d].width=%d\n", streaming_index, interface.width);
            Append(&text, "[probe] stream[%d].height=%d\n", streaming_index, interface.height);
            Append(&text, "[probe] stream[%d].bytes_per_line=%u (derived from max_frame_bytes/height)\n",
                         streaming_index, interface.bytes_per_line);
            ++streaming_index;
        }
        if (interface.hid && control_printed == 0) {
            const ProbeEndpoint* endpoint = interface.endpoints.empty() ? nullptr : &interface.endpoints[0];
            Append(&text, "[probe] control.endpoint.interface_number=%d\n", interface.number);
            Append(&text, "[probe] control.endpoint.alternate_setting=%d\n", interface.alternate_setting);
            if (endpoint != nullptr) {
                Append(&text, "[probe] control.endpoint.endpoint_address=0x%02X\n", endpoint->address);
                Append(&text, "[probe] control.endpoint.transfer=%s\n",
                             TransferTypeName(endpoint->transfer));
            } else {
                Append(&text, "[probe] control.endpoint.endpoint_address=unknown\n");
                Append(&text, "[probe] control.endpoint.transfer=unknown\n");
            }
            Append(&text, "[probe] control.report_descriptor_length=%d\n",
                         interface.hid_report_descriptor_length);
            if (!result.hid_report_ids.empty()) {
                Append(&text, "[probe] control.report_ids=");
                for (size_t i = 0; i < result.hid_report_ids.size(); ++i) {
                    Append(&text, "%s%d", i ? "," : "", result.hid_report_ids[i]);
                }
                Append(&text, "\n");
            } else {
                Append(&text, "[probe] control.report_id=unknown (needs the report descriptor)\n");
            }
            control_printed = 1;
        }
    }
    Append(&text, "[probe] note: the fields printed above are the table's names; the IR/depth "
                      "assignment needs the RPC->endpoint map (OQ-169e).\n");
    return text;
}

void PrintProbe(const ProbeResult& result, std::FILE* out) {
    const std::string text = FormatProbe(result);
    std::fwrite(text.data(), 1, text.size(), out);
    std::fflush(out);
}

}  // namespace raw_capture
