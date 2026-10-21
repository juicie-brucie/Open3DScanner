// Row #182: read the scanner's interface observations from the IORegistry.
//
// This is the read-only path that works while the vendor daemon holds the USB
// device: no IOUSBHost open, no claim, no configuration change, no traffic.
// The IORegistry publishes one IOUSBHostInterface node per interface, with
// bInterfaceNumber/bAlternateSetting/bInterfaceClass/bInterfaceSubClass/
// bNumEndpoints and the interface's string descriptor (kUSBString), which is
// where the "IR Camera"/"Depth Camera"/"RGB Camera"/"IMU" names come from.

#include "correlation.h"

#if defined(__APPLE__)

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>

#include <algorithm>
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

int PropertyInt(io_service_t service, CFStringRef key, int fallback) {
    CFTypeRef value = IORegistryEntryCreateCFProperty(service, key, kCFAllocatorDefault, 0);
    if (value == nullptr) return fallback;
    int number = fallback;
    if (CFGetTypeID(value) == CFNumberGetTypeID()) {
        CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberIntType, &number);
    }
    CFRelease(value);
    return number;
}

std::string PropertyString(io_service_t service, CFStringRef key) {
    CFTypeRef value = IORegistryEntryCreateCFProperty(service, key, kCFAllocatorDefault, 0);
    if (value == nullptr) return "";
    std::string text;
    if (CFGetTypeID(value) == CFStringGetTypeID()) {
        const CFIndex length = CFStringGetLength(static_cast<CFStringRef>(value));
        const CFIndex capacity = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
        std::vector<char> buffer(static_cast<size_t>(capacity), 0);
        if (CFStringGetCString(static_cast<CFStringRef>(value), buffer.data(), capacity,
                               kCFStringEncodingUTF8)) {
            text = buffer.data();
        }
    }
    CFRelease(value);
    return text;
}

}  // namespace

bool ReadScannerInterfaces(std::vector<InterfaceObservation>* interfaces, std::string* error) {
    interfaces->clear();
    error->clear();
    CFMutableDictionaryRef matching = IOServiceMatching("IOUSBHostInterface");
    if (matching == nullptr) {
        *error = "IOServiceMatching(IOUSBHostInterface) failed";
        return false;
    }
    io_iterator_t iterator = 0;
    const kern_return_t kr = IOServiceGetMatchingServices(MainPort(), matching, &iterator);
    if (kr != KERN_SUCCESS) {
        *error = "cannot enumerate IOUSBHostInterface services";
        return false;
    }
    io_service_t service = 0;
    while ((service = IOIteratorNext(iterator)) != 0) {
        const int vendor = PropertyInt(service, CFSTR("idVendor"), -1);
        if (vendor != static_cast<int>(Table().vendor_id)) {
            IOObjectRelease(service);
            continue;
        }
        InterfaceObservation observation;
        observation.number = PropertyInt(service, CFSTR("bInterfaceNumber"), kUnset);
        observation.alternate_setting = PropertyInt(service, CFSTR("bAlternateSetting"), kUnset);
        observation.class_code = PropertyInt(service, CFSTR("bInterfaceClass"), 0);
        observation.subclass = PropertyInt(service, CFSTR("bInterfaceSubClass"), 0);
        observation.num_endpoints = PropertyInt(service, CFSTR("bNumEndpoints"), 0);
        observation.name = PropertyString(service, CFSTR("kUSBString"));
        if (observation.name.empty()) observation.name = PropertyString(service, CFSTR("USB Product Name"));
        interfaces->push_back(observation);
        IOObjectRelease(service);
    }
    IOObjectRelease(iterator);
    std::sort(interfaces->begin(), interfaces->end(),
              [](const InterfaceObservation& a, const InterfaceObservation& b) {
                  return a.number < b.number;
              });
    if (interfaces->empty()) {
        *error = "no IOUSBHostInterface nodes with the scanner's VID (is the scanner attached?)";
        return false;
    }
    return true;
}

}  // namespace raw_capture

#endif  // __APPLE__
