// Row #178: the live macOS backends behind #171's seams — the UVC frame
// source and the HID/vendor command transport — over IOUSBHost.
//
// They use descriptor_table.h and nothing else for the device facts: an
// interface/endpoint/format the table does not have is a named refusal, never
// a guess. The read path is bulk/interrupt only; an isochronous endpoint is
// refused by name because this build's synchronous IOUSBHost path does not
// service isochronous URBs.

#include "raw_capture.h"

#if defined(__APPLE__)

#import <IOUSBHost/IOUSBHost.h>

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/usb/IOUSBLib.h>

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "hid_report.h"
#include "uvc_frames.h"

namespace raw_capture {
namespace {

mach_port_t MainPort() {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    return kIOMasterPortDefault;
#pragma clang diagnostic pop
}

io_service_t FindScannerService() {
    CFMutableDictionaryRef matching = IOServiceMatching(kIOUSBDeviceClassName);
    if (matching == nullptr) return 0;
    int vendor = Table().vendor_id;
    int product = Table().product_id;
    CFNumberRef vendor_ref = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &vendor);
    CFNumberRef product_ref = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &product);
    if (vendor_ref) CFDictionarySetValue(matching, CFSTR(kUSBVendorID), vendor_ref);
    if (product_ref) CFDictionarySetValue(matching, CFSTR(kUSBProductID), product_ref);
    if (vendor_ref) CFRelease(vendor_ref);
    if (product_ref) CFRelease(product_ref);
    return IOServiceGetMatchingService(MainPort(), matching);
}

// Finds a specific interface's own IOKit service directly, by vendor/product
// ID and interface number. Built with the exact same low-level
// IOServiceMatching + CFDictionarySetValue pattern as FindScannerService()
// above — not IOUSBHostInterface's own
// createMatchingDictionaryWithVendorID:...interfaceNumber:... convenience
// method, which hw-test found returns no match (interface_service == 0) 15
// retries running, even though `ioreg -p IOService` independently confirmed
// a fully "registered, matched, active" IOUSBHostInterface node with those
// exact idVendor/idProduct/bInterfaceNumber property values
// (2026-10-24-plan-uvc-findseize.md, -devicecapture.md). Whatever that
// convenience method builds differs from this in some way that keeps it
// from matching; this uses the identical shape already proven to work for
// the device-level lookup, with the class name and the interface-number key
// string both taken from IOKit's own headers
// (kIOUSBHostInterfaceClassName="IOUSBHostInterface",
// kUSBInterfaceNumber="bInterfaceNumber" via
// IOUSBHostFamilyDefinitions.h/USBSpec.h) rather than assumed.
io_service_t FindInterfaceService(int interface_number) {
    CFMutableDictionaryRef matching = IOServiceMatching(kIOUSBHostInterfaceClassName);
    if (matching == nullptr) return 0;
    int vendor = Table().vendor_id;
    int product = Table().product_id;
    CFNumberRef vendor_ref = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &vendor);
    CFNumberRef product_ref = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &product);
    CFNumberRef interface_ref = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &interface_number);
    if (vendor_ref) CFDictionarySetValue(matching, CFSTR(kUSBVendorID), vendor_ref);
    if (product_ref) CFDictionarySetValue(matching, CFSTR(kUSBProductID), product_ref);
    if (interface_ref) CFDictionarySetValue(matching, CFSTR(kUSBInterfaceNumber), interface_ref);
    if (vendor_ref) CFRelease(vendor_ref);
    if (product_ref) CFRelease(product_ref);
    if (interface_ref) CFRelease(interface_ref);
    return IOServiceGetMatchingService(MainPort(), matching);
}

std::string NsErrorText(NSError* error) {
    if (error == nil) return "unknown error";
    return std::string([[error localizedDescription] UTF8String]);
}

bool IsTimeout(NSError* error) {
    return error != nil && error.code == static_cast<NSInteger>(kIOReturnTimeout);
}

// UVC class-specific request codes (UVC 1.1 Table 4-75) and VideoStreaming
// control selectors (UVC 1.1 Table 4-47's VS_PROBE_CONTROL/VS_COMMIT_CONTROL,
// §4.3.1.1 / §4.3.1.2). Sent as a class request to the interface recipient
// (bmRequestType), never the device — UVC control requests target whichever
// VideoStreaming interface number is in wIndex.
constexpr uint8_t kUvcRequestSetCur = 0x01;
constexpr uint8_t kUvcRequestGetCur = 0x81;
constexpr uint16_t kUvcVsProbeControl = 0x01;
constexpr uint16_t kUvcVsCommitControl = 0x02;

// UVC 1.1 Video Probe and Commit Control (UVC 1.1 §4.3.1.1 / Table 4-47,
// 34 bytes) — mandatory negotiation on a VideoStreaming interface before the
// device will accept reads on a streaming endpoint. Hw-test confirmed its
// absence as the cause of "the endpoint read failed: Unable to send IO" on
// every capture attempt (2026-10-24-plan-uvc-retest.md,
// -clearstall.md): neither the alt-setting fix nor a clear-stall attempt
// changed that error; this device simply refuses to stream until probed and
// committed. The 34-byte length is read from the device's own VC_HEADER
// (bcdUVC=0x0110, UVC 1.1 — descriptor_table.h's bcd_uvc), not assumed.
struct VideoProbeCommitControl {
    uint16_t bm_hint = 0;
    uint8_t format_index = 0;
    uint8_t frame_index = 0;
    uint32_t frame_interval = 0;
    uint16_t key_frame_rate = 0;
    uint16_t p_frame_rate = 0;
    uint16_t comp_quality = 0;
    uint16_t comp_window_size = 0;
    uint16_t delay = 0;
    uint32_t max_video_frame_size = 0;
    uint32_t max_payload_transfer_size = 0;
    uint32_t clock_frequency = 0;
    uint8_t framing_info = 0;
    uint8_t preferred_version = 0;
    uint8_t min_version = 0;
    uint8_t max_version = 0;
};

void Put16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v & 0xff);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xff);
}
void Put32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v & 0xff);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xff);
    p[2] = static_cast<uint8_t>((v >> 16) & 0xff);
    p[3] = static_cast<uint8_t>((v >> 24) & 0xff);
}
uint16_t Get16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t Get32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

// Writes exactly kVideoProbeCommitControlSize (34) bytes to `buf`.
void SerializeProbeCommit(const VideoProbeCommitControl& c, uint8_t* buf) {
    Put16(buf + 0, c.bm_hint);
    buf[2] = c.format_index;
    buf[3] = c.frame_index;
    Put32(buf + 4, c.frame_interval);
    Put16(buf + 8, c.key_frame_rate);
    Put16(buf + 10, c.p_frame_rate);
    Put16(buf + 12, c.comp_quality);
    Put16(buf + 14, c.comp_window_size);
    Put16(buf + 16, c.delay);
    Put32(buf + 18, c.max_video_frame_size);
    Put32(buf + 22, c.max_payload_transfer_size);
    Put32(buf + 26, c.clock_frequency);
    buf[30] = c.framing_info;
    buf[31] = c.preferred_version;
    buf[32] = c.min_version;
    buf[33] = c.max_version;
}

// Reads whatever prefix of the 34-byte layout `len` bytes actually cover —
// a shorter reply (a UVC 1.0 device, 26 bytes) still yields its real fields
// instead of an out-of-bounds read.
VideoProbeCommitControl DeserializeProbeCommit(const uint8_t* buf, size_t len) {
    VideoProbeCommitControl c;
    if (len >= 2) c.bm_hint = Get16(buf + 0);
    if (len >= 3) c.format_index = buf[2];
    if (len >= 4) c.frame_index = buf[3];
    if (len >= 8) c.frame_interval = Get32(buf + 4);
    if (len >= 10) c.key_frame_rate = Get16(buf + 8);
    if (len >= 12) c.p_frame_rate = Get16(buf + 10);
    if (len >= 14) c.comp_quality = Get16(buf + 12);
    if (len >= 16) c.comp_window_size = Get16(buf + 14);
    if (len >= 18) c.delay = Get16(buf + 16);
    if (len >= 22) c.max_video_frame_size = Get32(buf + 18);
    if (len >= 26) c.max_payload_transfer_size = Get32(buf + 22);
    if (len >= 30) c.clock_frequency = Get32(buf + 26);
    if (len >= 31) c.framing_info = buf[30];
    if (len >= 32) c.preferred_version = buf[31];
    if (len >= 33) c.min_version = buf[32];
    if (len >= 34) c.max_version = buf[33];
    return c;
}

// Opens the device, the named interface, selects the alternate setting and
// takes the named endpoint's pipe. Everything is released by Close().
class UsbInterfaceHandle {
public:
    ~UsbInterfaceHandle() { Close(); }

    bool Open(const EndpointSpec& spec, std::string* why) {
        Close();
        io_service_t service = FindScannerService();
        if (service == 0) {
            *why = "no scanner USB device found (plugged in? powered?)";
            return false;
        }
        NSError* error = nil;
        // Plain open (no DeviceCapture) — the device object itself has no
        // competing claimant; ioreg -p IOService (not -p IOUSB, which
        // doesn't show these nodes at all) confirms that. The exclusive
        // owner is at the INTERFACE level: Apple's own UVCAssistant
        // (com.apple.cmio.uvcassistantextension) holds "Creality Raptor
        // Pro Depth/IR/RGB Camera@N" interfaces, and
        // AppleUserUSBHostHIDDevice holds the IMU interface, both the
        // moment the device enumerates
        // (2026-10-24-plan-uvc-devicecapture.md's own ioreg output:
        // "UsbExclusiveOwner" = "pid 588, UVCAssistant" on every video
        // interface node). DeviceCapture at the device level does not
        // evict an interface-level exclusive owner — hence it made no
        // difference. See the interface claim below for the actual fix.
        device_ = [[IOUSBHostDevice alloc] initWithIOService:service
                                                    options:IOUSBHostObjectInitOptionsNone
                                                      queue:nil
                                                      error:&error
                                            interestHandler:nil];
        if (device_ == nil) {
            *why = "the USB device could not be opened: " + NsErrorText(error) +
                   " (stop the vendor daemon: it holds the device)";
            return false;
        }
        // Find the interface's own IOKit service directly by vendor/product/
        // interface-number matching (FindInterfaceService), then claim it
        // with DeviceSeize — not None — because the node's own
        // "UsbExclusiveOwner" names a macOS system extension (UVCAssistant
        // for video, AppleUserUSBHostHIDDevice for the IMU) that claims it
        // the instant the device enumerates, independent of anything
        // claimed on the top-level device.
        // IOUSBHostObjectInitOptionsDeviceSeize's own header comment: "will
        // request the current owner of the underlying kernel service to
        // close via kUSBHostMessageDeviceIsRequestingClose" — unlike
        // DeviceCapture, its doc names no entitlement or root requirement,
        // since it is a polite close request the current owner can honor,
        // not a forced takeover.
        //
        // Both the find and the open are retried together, bounded: a
        // static ioreg snapshot (taken well after a prior run had already
        // settled) showed the node present, but that does not prove it is
        // there at the exact instant a fresh process opens the device —
        // hw-test found the lookup itself fail outright, on its first and
        // only attempt, with no retry wrapping it
        // (2026-10-24-plan-uvc-seize.md). The original registry-publish-
        // race hypothesis from earlier in this investigation may still be
        // real, layered underneath the UVCAssistant ownership issue, so
        // retry the whole find-then-seize sequence rather than assuming
        // either half is reliable on its own.
        constexpr int kInterfaceClaimAttempts = 15;
        constexpr auto kInterfaceClaimRetryDelay = std::chrono::milliseconds(100);
        std::string last_why;
        for (int attempt = 0; attempt < kInterfaceClaimAttempts && interface_ == nil; ++attempt) {
            if (attempt > 0) std::this_thread::sleep_for(kInterfaceClaimRetryDelay);
            io_service_t interface_service = FindInterfaceService(spec.interface_number);
            if (interface_service == 0) {
                last_why = "interface " + std::to_string(spec.interface_number) + " is not on the device";
                continue;
            }
            interface_ = [[IOUSBHostInterface alloc] initWithIOService:interface_service
                                                                options:IOUSBHostObjectInitOptionsDeviceSeize
                                                                  queue:nil
                                                                  error:&error
                                                        interestHandler:nil];
            IOObjectRelease(interface_service);
            if (interface_ == nil) {
                last_why = "interface " + std::to_string(spec.interface_number) +
                           " could not be seized from its current owner: " + NsErrorText(error);
            }
        }
        if (interface_ == nil) {
            *why = last_why + " (retried " + std::to_string(kInterfaceClaimAttempts) + " times over ~" +
                   std::to_string(kInterfaceClaimAttempts * kInterfaceClaimRetryDelay.count()) + "ms)";
            return false;
        }
        // Claiming an interface leaves it at its current alternate setting
        // (USB 2.0 §9.4.10: SetConfiguration defaults every interface to
        // alt 0, and nothing here has switched it since). This device's
        // streaming/HID interfaces carry exactly one alternate-setting
        // descriptor (alt 0; confirmed by the descriptor probe, every
        // interface prints alternate_setting=0 with no sibling altsetting
        // entries) and its firmware does not accept an explicit
        // SET_INTERFACE for that already-active setting — the call fails
        // with "could not be selected" even for alt 0 == alt 0. Skip the
        // request when the interface is already at the target setting;
        // only issue SET_INTERFACE for an actual switch.
        const IOUSBInterfaceDescriptor* active = interface_.interfaceDescriptor;
        if (active == nullptr || active->bAlternateSetting != spec.alternate_setting) {
            if (![interface_ selectAlternateSetting:static_cast<NSUInteger>(spec.alternate_setting)
                                              error:&error]) {
                *why = "alternate setting " + std::to_string(spec.alternate_setting) +
                       " could not be selected: " + NsErrorText(error);
                return false;
            }
        }
        pipe_ = [interface_ copyPipeWithAddress:static_cast<NSUInteger>(spec.endpoint_address) error:&error];
        if (pipe_ == nil) {
            *why = "endpoint 0x" + std::to_string(spec.endpoint_address) + " has no pipe: " +
                   NsErrorText(error);
            return false;
        }
        // The vendor daemon (if it was holding this device) was stopped by
        // `launchctl bootout`, not a clean close — its in-flight bulk/interrupt
        // transfer, if any, can leave the endpoint halted at the controller.
        // A halted endpoint refuses further IO until explicitly cleared (Apple's
        // own IOUSBHostPipe docs: "any IO error other than a timeout" halts a
        // bulk/interrupt endpoint, and clearStall resets it, including the data
        // toggle). Attempt it unconditionally and ignore failure here: a pipe
        // that was never stalled may reject the request, which is not an error
        // for our purposes — only the read that follows tells us whether IO
        // actually works now.
        [pipe_ clearStallWithError:nil];
        return true;
    }

    // Re-fetches the pipe for `endpoint_address` on the already-claimed
    // interface, discarding whatever pipe object Open() returned. Apple's
    // own IOUSBHostInterface docs say selectAlternateSetting: invalidates
    // existing pipes because switching the active configuration is expected
    // to; UVC's SET_CUR(VS_COMMIT_CONTROL) is the bulk-transfer analogue of
    // that — "activate this stream configuration" — and hw-test found a
    // pipe obtained before a successful negotiation still refuses reads
    // afterward (2026-10-24-plan-uvc-probecommit.md: negotiation completed
    // with no error, the read failed anyway, unchanged from before
    // negotiation existed). Call after NegotiateUvcFormat succeeds, before
    // the first Read().
    bool RefreshPipe(int endpoint_address, std::string* why) {
        if (interface_ == nil) {
            *why = "no interface claimed; call Open() first";
            return false;
        }
        if (pipe_ != nil) {
            [pipe_ release];
            pipe_ = nil;
        }
        NSError* error = nil;
        pipe_ = [interface_ copyPipeWithAddress:static_cast<NSUInteger>(endpoint_address) error:&error];
        if (pipe_ == nil) {
            *why = "endpoint 0x" + std::to_string(endpoint_address) + " has no pipe after commit: " +
                   NsErrorText(error);
            return false;
        }
        [pipe_ clearStallWithError:nil];
        return true;
    }

    void Close() {
        if (pipe_ != nil) {
            [pipe_ release];
            pipe_ = nil;
        }
        if (interface_ != nil) {
            [interface_ release];
            interface_ = nil;
        }
        if (device_ != nil) {
            [device_ release];
            device_ = nil;
        }
    }

    bool is_open() const { return pipe_ != nil; }
    IOUSBHostPipe* pipe() const { return pipe_; }

    // Reads one transfer. Returns false with `why` on failure.
    bool Read(std::vector<uint8_t>* bytes, size_t capacity, TransferType transfer, std::string* why) {
        bytes->resize(capacity);
        NSMutableData* data = [NSMutableData dataWithLength:static_cast<NSUInteger>(capacity)];
        NSUInteger moved = 0;
        NSError* error = nil;
        const NSTimeInterval timeout = transfer == TransferType::kInterrupt ? 0.0 : 1.0;
        if (![pipe_ sendIORequestWithData:data
                         bytesTransferred:&moved
                        completionTimeout:timeout
                                    error:&error]) {
            *why = "the endpoint read failed: " + NsErrorText(error);
            return false;
        }
        bytes->resize(static_cast<size_t>(moved));
        return true;
    }

    // UVC 1.1 §4.3.1.1: GET_CUR(PROBE) to read the device's current
    // negotiation state, overwrite only format/frame/interval, SET_CUR(PROBE)
    // to propose it, GET_CUR(PROBE) again to read back what the device
    // actually accepted (it may adjust dwMaxVideoFrameSize /
    // dwMaxPayloadTransferSize), then SET_CUR(COMMIT) with the accepted
    // struct — never the proposed one. Must be called after Open() succeeds
    // and before the first Read().
    bool NegotiateUvcFormat(int interface_number, int format_index, int frame_index,
                             uint32_t frame_interval_100ns, int probe_commit_control_length,
                             std::string* why) {
        if (interface_ == nil) {
            *why = "no interface claimed; call Open() first";
            return false;
        }
        const NSUInteger length = static_cast<NSUInteger>(probe_commit_control_length);
        auto send = [&](uint8_t request, uint16_t control_selector, NSMutableData* data,
                         std::string* step_why) -> bool {
            IOUSBDeviceRequest req;
            req.bmRequestType = static_cast<uint8_t>(
                (request == kUvcRequestGetCur ? kIOUSBDeviceRequestDirectionIn
                                               : kIOUSBDeviceRequestDirectionOut) |
                kIOUSBDeviceRequestTypeClass | kIOUSBDeviceRequestRecipientInterface);
            req.bRequest = request;
            req.wValue = static_cast<uint16_t>(control_selector << 8);
            req.wIndex = static_cast<uint16_t>(interface_number);
            req.wLength = static_cast<uint16_t>(length);
            NSError* error = nil;
            NSUInteger moved = 0;
            if (![interface_ sendDeviceRequest:req
                                           data:data
                               bytesTransferred:&moved
                              completionTimeout:1.0
                                          error:&error]) {
                *step_why = NsErrorText(error);
                return false;
            }
            return true;
        };

        std::string step_why;
        NSMutableData* probe_data = [NSMutableData dataWithLength:length];
        if (!send(kUvcRequestGetCur, kUvcVsProbeControl, probe_data, &step_why)) {
            *why = "GET_CUR(VS_PROBE_CONTROL) failed: " + step_why;
            return false;
        }
        VideoProbeCommitControl proposed = DeserializeProbeCommit(
            static_cast<const uint8_t*>(probe_data.bytes), probe_data.length);
        proposed.format_index = static_cast<uint8_t>(format_index);
        proposed.frame_index = static_cast<uint8_t>(frame_index);
        proposed.frame_interval = frame_interval_100ns;
        proposed.bm_hint = 0x01;  // bit 0: dwFrameInterval is the field we are specifying

        std::vector<uint8_t> wire(length, 0);
        SerializeProbeCommit(proposed, wire.data());
        NSMutableData* set_data = [NSMutableData dataWithBytes:wire.data() length:length];
        if (!send(kUvcRequestSetCur, kUvcVsProbeControl, set_data, &step_why)) {
            *why = "SET_CUR(VS_PROBE_CONTROL) failed: " + step_why;
            return false;
        }

        // Read back what the device actually accepted — the commit step
        // must use the device's own accepted struct, not the one proposed.
        NSMutableData* accepted_data = [NSMutableData dataWithLength:length];
        if (!send(kUvcRequestGetCur, kUvcVsProbeControl, accepted_data, &step_why)) {
            *why = "GET_CUR(VS_PROBE_CONTROL) (read-back) failed: " + step_why;
            return false;
        }
        if (!send(kUvcRequestSetCur, kUvcVsCommitControl, accepted_data, &step_why)) {
            *why = "SET_CUR(VS_COMMIT_CONTROL) failed: " + step_why;
            return false;
        }
        return true;
    }

private:
    IOUSBHostDevice* device_ = nil;
    IOUSBHostInterface* interface_ = nil;
    IOUSBHostPipe* pipe_ = nil;
};

std::string MissingStreamFields(const StreamSpec& spec) {
    std::string missing;
    auto add = [&missing](const char* field) {
        if (!missing.empty()) missing += ", ";
        missing += field;
    };
    if (spec.endpoint.interface_number == kUnset) add("interface_number");
    if (spec.endpoint.alternate_setting == kUnset) add("alternate_setting");
    if (spec.endpoint.endpoint_address == kUnset) add("endpoint_address");
    if (spec.endpoint.transfer == TransferType::kUnknown) add("transfer");
    if (spec.pixel_format == 0) add("pixel_format");
    if (spec.bytes_per_line == 0) add("bytes_per_line");
    if (spec.width == 0 || spec.height == 0) add("width/height");
    if (spec.format_index == kUnset) add("format_index");
    if (spec.frame_index == kUnset) add("frame_index");
    if (spec.frame_interval_100ns == 0) add("frame_interval_100ns");
    return missing;
}

class IokitUvcSource : public FrameSource {
public:
    explicit IokitUvcSource(StreamKind kind) : kind_(kind) {}

    bool configured() const override { return StreamConfigured(Table().stream(kind_)); }

    std::string configuration_error() const override {
        const StreamSpec& spec = Table().stream(kind_);
        const std::string missing = MissingStreamFields(spec);
        if (!missing.empty()) {
            return std::string("not-configured: ") + spec.name + " " + missing +
                   " (run raw_capture --probe and fill descriptor_table.h)";
        }
        if (spec.endpoint.transfer == TransferType::kIsochronous) {
            return "not-configured: the endpoint is isochronous; this build reads bulk/interrupt only";
        }
        return "";
    }

    bool Next(Frame* frame, std::string* error) override {
        error->clear();
        if (!configured()) {
            *error = configuration_error();
            return false;
        }
        const StreamSpec& spec = Table().stream(kind_);
        if (spec.endpoint.transfer == TransferType::kIsochronous) {
            *error = configuration_error();
            return false;
        }
        if (assembler_ == nullptr) {
            assembler_ = std::make_unique<UvcAssembler>(spec.bytes_per_line * spec.height);
            if (!handle_.Open(spec.endpoint, error)) return false;
            // UVC 1.1 §4.3.1.1: the device refuses streaming reads until this
            // negotiation completes (hw-test-confirmed; see the struct's own
            // comment). Must happen after Open(), before the first Read().
            if (!handle_.NegotiateUvcFormat(spec.endpoint.interface_number, spec.format_index,
                                             spec.frame_index, spec.frame_interval_100ns,
                                             Table().probe_commit_control_length, error)) {
                return false;
            }
            // The commit may have invalidated the pipe Open() returned (see
            // RefreshPipe's own comment) — fetch a fresh one before reading.
            if (!handle_.RefreshPipe(spec.endpoint.endpoint_address, error)) return false;
            capacity_ = std::min<size_t>(static_cast<size_t>(spec.bytes_per_line) * spec.height, 4u << 20);
            capacity_ = std::max<size_t>(capacity_, 64u << 10);
        }
        // One frame may span several transfers; bound the loop so a stalled
        // stream is an error rather than a hang.
        for (int attempt = 0; attempt < 64; ++attempt) {
            std::vector<uint8_t> transfer;
            if (!handle_.Read(&transfer, capacity_, spec.endpoint.transfer, error)) return false;
            if (transfer.empty()) continue;
            UvcFrame assembled;
            std::string uvc_error;
            if (!assembler_->Feed(transfer.data(), transfer.size(), &assembled, &uvc_error)) {
                if (!uvc_error.empty()) {
                    // A damaged payload is counted and skipped, not fatal.
                    continue;
                }
                continue;
            }
            frame->kind = kind_;
            frame->seq = frame_seq_++;
            frame->timestamp_us = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count());
            frame->width = spec.width;
            frame->height = spec.height;
            frame->stride = spec.bytes_per_line;
            frame->format = spec.pixel_format;
            frame->data = std::move(assembled.data);
            return true;
        }
        *error = "no complete frame within 64 endpoint reads";
        return false;
    }

private:
    StreamKind kind_;
    UsbInterfaceHandle handle_;
    std::unique_ptr<UvcAssembler> assembler_;
    size_t capacity_ = 0;
    uint64_t frame_seq_ = 0;
};

class IokitHidTransport : public CommandTransport {
public:
    bool configured() const override {
        const ControlSpec& spec = Table().control;
        return ControlConfigured(spec) && spec.report_id >= 0 && spec.report_length > 0;
    }

    std::string configuration_error() const override {
        const ControlSpec& spec = Table().control;
        if (!ControlConfigured(spec)) return std::string("not-configured: ") + spec.missing;
        if (spec.report_id < 0 || spec.report_length <= 0) {
            return "not-configured: the HID report id/length are not in the table";
        }
        return "";
    }

    CommandStatus Send(uint32_t property, double value) override {
        const ControlSpec& spec = Table().control;
        std::vector<uint8_t> report;
        std::string why;
        if (!BuildHidReport(spec, property, value, &report, &why)) {
            return why.rfind("not-configured", 0) == 0 ? CommandStatus::kNotConfigured
                                                       : CommandStatus::kRejected;
        }
        if (!handle_.is_open() && !handle_.Open(spec.endpoint, &why)) {
            detail_ = why;
            return CommandStatus::kTransportError;
        }
        NSMutableData* data = [NSMutableData dataWithBytes:report.data() length:report.size()];
        NSUInteger moved = 0;
        NSError* error = nil;
        const NSTimeInterval timeout = spec.endpoint.transfer == TransferType::kInterrupt ? 0.0 : 1.0;
        if (![handle_.pipe() sendIORequestWithData:data
                                 bytesTransferred:&moved
                                completionTimeout:timeout
                                            error:&error]) {
            detail_ = NsErrorText(error);
            return IsTimeout(error) ? CommandStatus::kTimeout : CommandStatus::kTransportError;
        }
        return CommandStatus::kAccepted;
    }

private:
    UsbInterfaceHandle handle_;
    std::string detail_;
};

}  // namespace

std::unique_ptr<FrameSource> MakeLiveUvcSource(StreamKind kind, std::string* why) {
    auto source = std::make_unique<IokitUvcSource>(kind);
    if (!source->configured()) {
        if (why) *why = source->configuration_error();
        return nullptr;
    }
    if (why) why->clear();
    return source;
}

std::unique_ptr<CommandTransport> MakeLiveHidTransport(std::string* why) {
    auto transport = std::make_unique<IokitHidTransport>();
    if (!transport->configured()) {
        if (why) *why = transport->configuration_error();
        return nullptr;
    }
    if (why) why->clear();
    return transport;
}

namespace {

// Reads an integer IORegistry property, or `fallback` if absent/not an int.
long RegistryInt(io_service_t service, CFStringRef key, long fallback) {
    CFTypeRef value = IORegistryEntryCreateCFProperty(service, key, kCFAllocatorDefault, 0);
    long out = fallback;
    if (value != nullptr) {
        if (CFGetTypeID(value) == CFNumberGetTypeID()) {
            CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberLongType, &out);
        }
        CFRelease(value);
    }
    return out;
}

// Reads a string IORegistry property into UTF-8, or "" if absent/not a string.
std::string RegistryString(io_service_t service, CFStringRef key) {
    CFTypeRef value = IORegistryEntryCreateCFProperty(service, key, kCFAllocatorDefault, 0);
    std::string out;
    if (value != nullptr) {
        if (CFGetTypeID(value) == CFStringGetTypeID()) {
            char buffer[256];
            if (CFStringGetCString(static_cast<CFStringRef>(value), buffer, sizeof(buffer),
                                   kCFStringEncodingUTF8)) {
                out = buffer;
            }
        }
        CFRelease(value);
    }
    return out;
}

}  // namespace

std::string DiagnoseVisibleInterfaces() {
    std::string out;
    auto append = [&out](const char* fmt, ...) {
        char buf[512];
        va_list args;
        va_start(args, fmt);
        const int n = std::vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        if (n > 0) out.append(buf, std::min<size_t>(static_cast<size_t>(n), sizeof(buf) - 1));
    };

    // Bare match: every IOUSBHostInterface on the system, no vendor/product/
    // interface-number filter at all. The point is to see what THIS process
    // can enumerate, versus what ioreg (privileged) sees.
    CFMutableDictionaryRef matching = IOServiceMatching(kIOUSBHostInterfaceClassName);
    if (matching == nullptr) {
        return "[diag] IOServiceMatching(IOUSBHostInterface) returned null\n";
    }
    io_iterator_t iterator = 0;
    const kern_return_t kr = IOServiceGetMatchingServices(MainPort(), matching, &iterator);
    if (kr != KERN_SUCCESS) {
        append("[diag] IOServiceGetMatchingServices failed: 0x%08x\n", kr);
        return out;
    }

    const int scanner_vendor = Table().vendor_id;
    const int scanner_product = Table().product_id;
    int total = 0, scanner = 0;
    io_service_t service = 0;
    append("[diag] enumerating every IOUSBHostInterface visible to this process "
           "(no filter); scanner is vid=0x%04X pid=0x%04X\n", scanner_vendor, scanner_product);
    while ((service = IOIteratorNext(iterator)) != 0) {
        ++total;
        const long vendor = RegistryInt(service, CFSTR(kUSBVendorID), -1);
        const long product = RegistryInt(service, CFSTR(kUSBProductID), -1);
        const long iface = RegistryInt(service, CFSTR(kUSBInterfaceNumber), -1);
        const std::string owner = RegistryString(service, CFSTR("UsbExclusiveOwner"));
        const std::string product_name = RegistryString(service, CFSTR("USB Product Name"));
        const bool is_scanner = (vendor == scanner_vendor && product == scanner_product);
        if (is_scanner) ++scanner;
        append("[diag]  vid=0x%04lX pid=0x%04lX interface=%ld owner=\"%s\" name=\"%s\"%s\n",
               vendor, product, iface, owner.empty() ? "(none)" : owner.c_str(),
               product_name.c_str(), is_scanner ? "  <-- SCANNER" : "");
        IOObjectRelease(service);
    }
    IOObjectRelease(iterator);
    append("[diag] total IOUSBHostInterface nodes visible: %d; of those, scanner's: %d\n",
           total, scanner);
    if (scanner == 0) {
        append("[diag] VERDICT: the scanner's interfaces are INVISIBLE to this process, even with "
               "no filter — a per-device/DriverKit visibility gate, not a matching bug. If other "
               "cameras appear above but the scanner does not, the entitlement path is the fix.\n");
    } else {
        append("[diag] VERDICT: the scanner's interfaces ARE visible to this process (%d found) — "
               "the blocker is in the matching filter or the seize, not raw visibility.\n", scanner);
    }
    return out;
}

}  // namespace raw_capture

#endif  // __APPLE__
