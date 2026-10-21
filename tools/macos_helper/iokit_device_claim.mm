// The live device claim (row #170): exclusive ownership of the scanner's USB
// device, and nothing else. No interface is opened, no endpoint is created,
// no UVC/HID request is made, no frame is read — the claim is the whole of
// this file's device work.
//
// The modern IOUSBHost API is used because the legacy IOUSBDevice user client
// is not available for the devices the current stack exposes: on this machine
// the legacy path fails at IOCreatePlugInInterfaceForService with
// kIOReturnNoResources while the device is an IOUSBHostDevice. IOUSBHost
// establishes exclusive ownership at init and reports termination through the
// interest handler, which is exactly the claim and the unplug event.
//
// Opening needs the privileges the launchd daemon has. Without them the open
// is refused and the refusal is reported verbatim, which is what the operator
// gate reads.

#include "device_claim.h"

#if defined(__APPLE__)

#import <IOUSBHost/IOUSBHost.h>

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOMessage.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/usb/IOUSBLib.h>

#include <cstdio>
#include <mutex>
#include <string>

namespace openscanner::helper {
namespace {

// The scanner's USB identity, matching the device-enumeration entry in
// docs/recon/findings/protocol-spec.md. This is the only device fact here.
constexpr int kVendorId = 0x2BC5;
constexpr int kProductId = 0x0C03;

mach_port_t MainPort() {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    return kIOMasterPortDefault;
#pragma clang diagnostic pop
}

std::string Hex(IOReturn value) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "0x%08x", static_cast<unsigned>(value));
    return buffer;
}

std::string OpenRefusal(IOReturn value, bool seize) {
    std::string text = "the USB device open was refused (" + Hex(value) + ")";
    if (value == kIOReturnExclusiveAccess) {
        text += ": another process holds the scanner (stop it: that is the operator gate)";
    } else if (value == kIOReturnNotPrivileged) {
        text += ": the helper is not privileged (run it as the daemon, or under sudo)";
    } else if (!seize) {
        text += ": check the device is connected; --seize would ask its holder to close";
    }
    return text;
}

class IokitDeviceClaim : public DeviceClaim {
public:
    explicit IokitDeviceClaim(bool seize) : seize_(seize) {}
    ~IokitDeviceClaim() override { Release(); }

    const char* Name() const override { return "iokit-usb"; }

    bool Acquire(std::string* why) override {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if (device_) return true;

        CFMutableDictionaryRef matching = IOServiceMatching(kIOUSBDeviceClassName);
        if (!matching) {
            if (why) *why = "IOServiceMatching failed";
            return false;
        }
        int vendor = kVendorId;
        int product = kProductId;
        CFNumberRef vendor_ref = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &vendor);
        CFNumberRef product_ref = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &product);
        if (vendor_ref) CFDictionarySetValue(matching, CFSTR(kUSBVendorID), vendor_ref);
        if (product_ref) CFDictionarySetValue(matching, CFSTR(kUSBProductID), product_ref);
        if (vendor_ref) CFRelease(vendor_ref);
        if (product_ref) CFRelease(product_ref);

        // The matching dictionary is consumed by the call.
        io_service_t service = IOServiceGetMatchingService(MainPort(), matching);
        if (!service) {
            if (why) *why = "no scanner USB device found (plugged in? powered?)";
            return false;
        }

        NSError* error = nil;
        const IOUSBHostObjectInitOptions options =
            seize_ ? IOUSBHostObjectInitOptionsDeviceSeize : IOUSBHostObjectInitOptionsNone;
        IokitDeviceClaim* self = this;
        IOUSBHostDevice* device = [[IOUSBHostDevice alloc]
            initWithIOService:service
                      options:options
                        queue:nil
                        error:&error
              interestHandler:^(IOUSBHostObject*, uint32_t message_type, void*) {
                  // The device's service is terminating: the claim went away
                  // underneath us (unplug). Other interest messages are not
                  // claim events.
                  if (message_type == kIOMessageServiceIsTerminated) self->OnTerminated();
              }];
        if (!device) {
            // The object did not take the service reference; it is ours still.
            IOServiceClose(service);
            const IOReturn code = error ? static_cast<IOReturn>(error.code) : kIOReturnError;
            if (why) *why = OpenRefusal(code, seize_);
            return false;
        }
        // The IOUSBHostObject owns the io_service_t from here and releases it
        // with the object.
        device_ = device;
        device_gone_ = false;
        return true;
    }

    void Release() override {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if (!device_) return;
        // destroy() de-registers the interest handler and drops the user
        // client; the object's dealloc would do it too, but being explicit
        // means the device is released before the process exits.
        if (!device_gone_) [device_ destroy];
        [device_ release];
        device_ = nil;
        device_gone_ = false;
    }

private:
    void OnTerminated() {
        {
            std::lock_guard<std::recursive_mutex> lock(mutex_);
            device_gone_ = true;
        }
        // Outside the lock: the machine releases the claim, which re-enters
        // this object.
        NotifyUnplug();
    }

    const bool seize_;
    std::recursive_mutex mutex_;
    IOUSBHostDevice* device_ = nil;
    bool device_gone_ = false;
};

}  // namespace

std::unique_ptr<DeviceClaim> MakeIokitDeviceClaim(bool seize) {
    return std::make_unique<IokitDeviceClaim>(seize);
}

}  // namespace openscanner::helper

#endif  // __APPLE__
