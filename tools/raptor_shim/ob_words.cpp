#include "ob_words.h"

#include <ctime>

namespace shim {
namespace {

uint64_t ClockUs(clockid_t id) {
    struct timespec ts = {};
    if (clock_gettime(id, &ts) != 0) return 0;
    return (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)(ts.tv_nsec / 1000);
}

class SystemClocks : public HostClocks {
public:
    uint64_t RealtimeUs() const override { return ClockUs(CLOCK_REALTIME); }
    uint64_t MonotonicUs() const override { return ClockUs(CLOCK_MONOTONIC); }
};

}  // namespace

const HostClocks& SystemHostClocks() {
    static const SystemClocks clocks;
    return clocks;
}

bool UnwiredIrLightState::LightType(const Interface& iface, int32_t* out, std::string* why) const {
    (void)out;
    if (why) {
        *why = "not-configured: e10 (the " + iface.name +
               " frame's IR light type) has no source: the UVC transport does not report the device's "
               "light state, and no finding binds the laser/exposure state to it";
    }
    return false;
}

HostClockObWords::HostClockObWords(const HostClocks* clocks, const IrLightState* light)
    : clocks_(clocks), light_(light) {}

bool HostClockObWords::Configured(const Interface& iface, std::string* why) const {
    if (!clocks_) {
        if (why) *why = "not-configured: e1/e2 (the host clocks) have no source";
        return false;
    }
    if (!light_) {
        if (why) *why = "not-configured: e10 (the " + iface.name + " frame's IR light type) has no source";
        return false;
    }
    int32_t probe = 0;
    return light_->LightType(iface, &probe, why);
}

bool HostClockObWords::Words(const Interface& iface, uint64_t seq, uint64_t timestamp_us,
                             ObFrameWords* out) const {
    (void)seq;           // the frame's sequence number is not an OB word
    (void)timestamp_us;  // the UVC PTS is the device clock; see the header
    if (!clocks_) {
        return false;
    }
    ObFrameWords words;
    // The RPC's two time words are host clock domains (rpc-endpoint-map.md
    // §2.2): stamped here, at the frame's dequeue.
    words.e1 = clocks_->RealtimeUs();
    words.e2 = clocks_->MonotonicUs();
    // The observed OBFormat, on every one of the 380 replies.
    words.e3 = 255;
    int32_t light = 0;
    if (!light_ || !light_->LightType(iface, &light, nullptr)) return false;
    words.e10 = light;
    if (out) *out = words;
    return true;
}

}  // namespace shim
