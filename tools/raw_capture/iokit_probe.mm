// Row #178: the macOS probe — enumerate the attached scanner's USB tree with
// IOUSBHost and hand the descriptor bytes to the pure parser. It only reads
// descriptors; it does not seize the device, change its configuration, open
// an interface, or start any stream. If the vendor daemon holds the device,
// the open is refused and that refusal is reported verbatim.

#include "descriptor_probe.h"

#if defined(__APPLE__)

#import <IOUSBHost/IOUSBHost.h>

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/usb/IOUSBLib.h>

#include <cstring>
#include <string>
#include <vector>

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
    return IOServiceGetMatchingService(MainPort(), matching);  // consumes `matching`
}

std::string NsErrorText(NSError* error) {
    if (error == nil) return "unknown error";
    return std::string([[error localizedDescription] UTF8String]);
}

// The scanner's HID interface object, or nil. Caller releases it.
IOUSBHostInterface* FindInterface(io_service_t device_service, int interface_number) {
    io_iterator_t iterator = 0;
    if (IORegistryEntryGetChildIterator(device_service, kIOServicePlane, &iterator) != KERN_SUCCESS) {
        return nil;
    }
    IOUSBHostInterface* found = nil;
    io_service_t child = 0;
    while ((child = IOIteratorNext(iterator)) != 0) {
        if (!IOObjectConformsTo(child, "IOUSBHostInterface")) {
            IOObjectRelease(child);
            continue;
        }
        NSError* error = nil;
        IOUSBHostInterface* candidate = [[IOUSBHostInterface alloc]
            initWithIOService:child
                      options:IOUSBHostObjectInitOptionsNone
                        queue:nil
                        error:&error
              interestHandler:nil];
        IOObjectRelease(child);
        if (candidate == nil) continue;
        const IOUSBInterfaceDescriptor* descriptor = candidate.interfaceDescriptor;
        if (descriptor != nullptr && descriptor->bInterfaceNumber == interface_number) {
            found = candidate;
            break;
        }
        [candidate release];
    }
    IOObjectRelease(iterator);
    return found;
}

// Row #189: read the HID report descriptor with GET_DESCRIPTOR (report) on
// endpoint 0 — read-only, no writes, no seize. The device request is tried
// first; some devices gate class requests on the interface's own user client,
// so that is the fallback. Failure is named, never a silent empty list.
void FetchHidReport(io_service_t device_service, IOUSBHostDevice* device, int interface_number,
                    int declared_length, ProbeResult* result) {
    result->hid_report.clear();
    result->hid_report_error.clear();
    result->hid_report_ids.clear();
    if (declared_length <= 0) {
        result->hid_report_error =
            "not-configured: the HID descriptor declares no report descriptor length";
        return;
    }
    if (declared_length > 8192) {
        result->hid_report_error = "not-configured: the declared report descriptor length is implausible";
        return;
    }
    IOUSBDeviceRequest request = {};
    request.bmRequestType = 0x81;  // device-to-host | standard | interface
    request.bRequest = 0x06;       // GET_DESCRIPTOR
    request.wValue = static_cast<uint16_t>(0x22 << 8);  // report descriptor
    request.wIndex = static_cast<uint16_t>(interface_number);
    request.wLength = static_cast<uint16_t>(declared_length);

    NSError* last_error = nil;
    auto try_request = [&](IOUSBHostObject* object) -> bool {
        NSMutableData* data = [NSMutableData dataWithLength:static_cast<NSUInteger>(declared_length)];
        NSUInteger moved = 0;
        NSError* error = nil;
        if (![object sendDeviceRequest:request
                                  data:data
                      bytesTransferred:&moved
                     completionTimeout:1.0
                                 error:&error]) {
            last_error = error;
            return false;
        }
        if (moved == 0) {
            last_error = nil;
            return false;
        }
        result->hid_report.assign(static_cast<const uint8_t*>([data bytes]),
                                  static_cast<const uint8_t*>([data bytes]) + moved);
        return true;
    };

    if (!try_request(device)) {
        IOUSBHostInterface* interface = FindInterface(device_service, interface_number);
        if (interface != nil) {
            const bool ok = try_request(interface);
            [interface release];
            if (!ok && last_error == nil) {
                result->hid_report_error =
                    "not-configured: the HID report descriptor read returned no bytes";
                return;
            }
        }
    }
    if (result->hid_report.empty()) {
        result->hid_report_error =
            "not-configured: the HID report descriptor read was refused: " + NsErrorText(last_error);
        return;
    }
    std::string detail;
    result->hid_report_ids = ParseHidReportIds(result->hid_report.data(), result->hid_report.size(), &detail);
}

}  // namespace

bool ProbeAttachedDevice(ProbeResult* result) {
    @autoreleasepool {
        result->interfaces.clear();
        result->hid_report_ids.clear();
        result->hid_report.clear();
        result->hid_report_error.clear();
        result->error.clear();

        io_service_t service = FindScannerService();
        if (service == 0) {
            result->error =
                "no scanner USB device found (plugged in? powered? the vendor daemon may hold it)";
            return false;
        }
        NSError* error = nil;
        IOUSBHostDevice* device = [[IOUSBHostDevice alloc] initWithIOService:service
                                                                    options:IOUSBHostObjectInitOptionsNone
                                                                      queue:nil
                                                                      error:&error
                                                            interestHandler:nil];
        if (device == nil) {
            result->error = "the USB device could not be opened: " + NsErrorText(error) +
                            " (a vendor daemon holding it must be stopped first)";
            IOObjectRelease(service);
            return false;
        }
        const IOUSBDeviceDescriptor* device_descriptor = device.deviceDescriptor;
        if (device_descriptor != nullptr) {
            result->vendor_id = device_descriptor->idVendor;
            result->product_id = device_descriptor->idProduct;
        }
        const IOUSBConfigurationDescriptor* configuration = device.configurationDescriptor;
        if (configuration == nullptr) {
            result->error = "the device has no selected configuration (is it configured by the OS?)";
            [device release];
            IOObjectRelease(service);
            return false;
        }
        const size_t total = configuration->wTotalLength;
        if (total < 9 || total > (16u << 20)) {
            result->error = "the configuration descriptor's wTotalLength is out of range";
            [device release];
            IOObjectRelease(service);
            return false;
        }
        std::vector<uint8_t> blob(total);
        std::memcpy(blob.data(), configuration, total);

        std::string detail;
        const bool parsed = ParseConfigurationDescriptor(blob.data(), blob.size(), result, &detail);
        if (parsed) {
            bool hid_found = false;
            for (const ProbeInterface& interface : result->interfaces) {
                if (interface.hid) {
                    hid_found = true;
                    FetchHidReport(service, device, interface.number, interface.hid_report_descriptor_length,
                                   result);
                    break;
                }
            }
            if (!hid_found) {
                result->hid_report_error = "not-configured: the configuration declares no HID interface";
            }
        }
        [device release];
        IOObjectRelease(service);
        return parsed;
    }
}

}  // namespace raw_capture

#endif  // __APPLE__
