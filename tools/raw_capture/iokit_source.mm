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
#include <cstring>
#include <memory>
#include <string>
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

std::string NsErrorText(NSError* error) {
    if (error == nil) return "unknown error";
    return std::string([[error localizedDescription] UTF8String]);
}

bool IsTimeout(NSError* error) {
    return error != nil && error.code == static_cast<NSInteger>(kIOReturnTimeout);
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
        // Find the interface service below the device and match its number.
        io_iterator_t iterator = 0;
        if (IORegistryEntryGetChildIterator(service, kIOServicePlane, &iterator) != KERN_SUCCESS) {
            *why = "cannot enumerate the device's interfaces";
            return false;
        }
        io_service_t child = 0;
        while ((child = IOIteratorNext(iterator)) != 0) {
            if (!IOObjectConformsTo(child, "IOUSBHostInterface")) {
                IOObjectRelease(child);
                continue;
            }
            IOUSBHostInterface* candidate = [[IOUSBHostInterface alloc]
                initWithIOService:child
                          options:IOUSBHostObjectInitOptionsNone
                            queue:nil
                            error:&error
                  interestHandler:nil];
            IOObjectRelease(child);
            if (candidate == nil) continue;
            const IOUSBInterfaceDescriptor* descriptor = candidate.interfaceDescriptor;
            if (descriptor == nullptr || descriptor->bInterfaceNumber != spec.interface_number) {
                [candidate release];
                continue;
            }
            interface_ = candidate;
            break;
        }
        IOObjectRelease(iterator);
        if (interface_ == nil) {
            *why = "interface " + std::to_string(spec.interface_number) + " is not on the device";
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

}  // namespace raw_capture

#endif  // __APPLE__
