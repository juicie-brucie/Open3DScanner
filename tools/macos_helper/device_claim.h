#pragma once
// The device-claim backend the state machine drives (row #170).
//
// A claim is exclusive ownership of the scanner's USB device with no data
// path: no interface is opened, no frames are read, no UVC/HID traffic
// happens. Streaming is Phase D's later work, not this skeleton's.
//
// The interface exists so the state machine's tests are device-free: they
// drive a fake backend and assert the call counts. The live backend is
// iokit_device_claim.mm, and only the operator gate runs it (row #170).

#include <functional>
#include <memory>
#include <string>

namespace openscanner::helper {

class DeviceClaim {
public:
    virtual ~DeviceClaim() = default;

    // Short name for the log line, e.g. "iokit-usb".
    virtual const char* Name() const = 0;

    // Take the claim. Returns false and fills `why` when it cannot — most
    // usefully, when something else already holds the device. `why` is
    // logged verbatim, so it should say what the operator can do about it.
    virtual bool Acquire(std::string* why) = 0;

    // Drop the claim. Must be safe to call when no claim is held and safe to
    // call after the device has gone away (unplug).
    virtual void Release() = 0;

    // The backend calls this once when the device disappears underneath a
    // held claim. The state machine wires it to its unplug event.
    void SetUnplugHandler(std::function<void()> handler) { unplug_handler_ = std::move(handler); }

protected:
    void NotifyUnplug() const {
        if (unplug_handler_) unplug_handler_();
    }

private:
    std::function<void()> unplug_handler_;
};

// Refuses every claim. This is the skeleton's default when no real backend is
// selected, so a run that touched nothing can never look like a green claim
// (row #170's live gate is the operator's, not a stub's).
class NullDeviceClaim : public DeviceClaim {
public:
    const char* Name() const override { return "null"; }
    bool Acquire(std::string* why) override;
    void Release() override;
};

#if defined(__APPLE__)
// Matches the scanner's USB device and opens it exclusively (optionally
// seizing it from whatever holds it). Does not touch any interface.
//
// `seize=false` is the default and the safe direction: a running vendor daemon
// keeps its claim and this returns a clean refusal instead of killing it
// (row #170 forbids disturbing it).
std::unique_ptr<DeviceClaim> MakeIokitDeviceClaim(bool seize);
#endif

}  // namespace openscanner::helper
