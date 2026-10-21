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

// Fetch the HID report descriptor for one interface (GET_DESCRIPTOR,
// standard, interface recipient) and put its report ids in `result`.
void FetchHidReportIds(IOUSBHostDevice* device, int interface_number, int declared_length,
                       ProbeResult* result) {
    if (declared_length <= 0 || declared_length > 8192) return;
    IOUSBDeviceRequest request = {};
    request.bmRequestType = 0x81;  // device-to-host | standard | interface
    request.bRequest = 0x06;       // GET_DESCRIPTOR
    request.wValue = static_cast<uint16_t>(0x22 << 8);  // report descriptor
    request.wIndex = static_cast<uint16_t>(interface_number);
    request.wLength = static_cast<uint16_t>(declared_length);
    NSMutableData* data = [NSMutableData dataWithLength:static_cast<NSUInteger>(declared_length)];
    NSUInteger moved = 0;
    NSError* error = nil;
    if (![device sendDeviceRequest:request
                              data:data
                  bytesTransferred:&moved
                 completionTimeout:1.0
                             error:&error]) {
        return;  // the report ids stay unset; the printout says so
    }
    std::string detail;
    result->hid_report_ids =
        ParseHidReportIds(static_cast<const uint8_t*>([data bytes]), static_cast<size_t>(moved), &detail);
}

}  // namespace

bool ProbeAttachedDevice(ProbeResult* result) {
    @autoreleasepool {
        result->interfaces.clear();
        result->hid_report_ids.clear();
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
            return false;
        }
        const size_t total = configuration->wTotalLength;
        if (total < 9 || total > (16u << 20)) {
            result->error = "the configuration descriptor's wTotalLength is out of range";
            [device release];
            return false;
        }
        std::vector<uint8_t> blob(total);
        std::memcpy(blob.data(), configuration, total);

        std::string detail;
        const bool parsed = ParseConfigurationDescriptor(blob.data(), blob.size(), result, &detail);
        if (parsed) {
            for (const ProbeInterface& interface : result->interfaces) {
                if (interface.hid) {
                    FetchHidReportIds(device, interface.number, interface.hid_report_descriptor_length,
                                      result);
                    break;
                }
            }
        }
        [device release];
        return parsed;
    }
}

}  // namespace raw_capture

#endif  // __APPLE__
