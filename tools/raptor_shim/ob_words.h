#pragma once
// Row #180 — the OB frame words, from their settled sources.
//
// The semantics are docs/recon/findings/rpc-endpoint-map.md §2.2 (row #183),
// byte-backed by the #132 bundles (380 get_frame replies):
//
//   e1  CLOCK_REALTIME µs   never 0; the wall clock at the daemon's dequeue
//   e2  CLOCK_MONOTONIC µs  monotonic; the same instant on the monotonic clock
//   e3  255                 the OBFormat (255 on 380/380; not the profile id 9)
//   e10 3 or 4              the device's IR light type, per frame on both
//                           interfaces — not a per-stream constant
//
// On a live UVC frame the PTS is the *device* clock. The RPC's e1/e2 are the
// host's clock domains (the daemon fills them from the PAL frame's timestamp
// fields), so this provider stamps the host clocks at the frame's dequeue and
// does not write the device PTS into either word: labelling a device clock as
// CLOCK_REALTIME would be a guess. The PTS stays available to the provider as
// an input for a future device-time mapping.
//
// e10 has no source in the repo today: the device's per-frame light state is
// not reported by the UVC transport, and the control path's laser/exposure
// state is not bound to it by any finding. It is therefore an interface with a
// named refusal, not a constant.

#include <cstdint>
#include <string>

#include "frame_source.h"
#include "live_source.h"

namespace shim {

// The two host clocks the RPC's time words live in.
class HostClocks {
public:
    virtual ~HostClocks() = default;
    // False when either clock cannot be read: the words are then refused
    // rather than filled with a zero (e1 is never 0 in the 380 replies).
    virtual bool Ok() const = 0;
    virtual uint64_t RealtimeUs() const = 0;   // CLOCK_REALTIME, µs
    virtual uint64_t MonotonicUs() const = 0;  // CLOCK_MONOTONIC, µs
};

// The process's clocks (the production source).
const HostClocks& SystemHostClocks();

// The device's IR light type for the frame just dequeued (e10).
class IrLightState {
public:
    virtual ~IrLightState() = default;
    // False with a `not-configured: e10 …` reason when the state is unknown.
    virtual bool LightType(const Interface& iface, int32_t* out, std::string* why) const = 0;
};

// Nothing on this host reports the device's light state yet.
class UnwiredIrLightState : public IrLightState {
public:
    bool LightType(const Interface& iface, int32_t* out, std::string* why) const override;
};

class HostClockObWords : public ObFrameWordsProvider {
public:
    // `clocks` is required; `light` may be null, which makes e10 a refusal.
    HostClockObWords(const HostClocks* clocks, const IrLightState* light);

    bool Configured(const Interface& iface, std::string* why) const override;
    bool Words(const Interface& iface, uint64_t seq, uint64_t timestamp_us, ObFrameWords* out) const override;

private:
    const HostClocks* clocks_ = nullptr;
    const IrLightState* light_ = nullptr;
};

}  // namespace shim
