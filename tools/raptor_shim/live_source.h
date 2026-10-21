#pragma once
// Row #179 — the shim's live frame source.
//
// The shim's FrameSource seam (row #172) is filled by BundleSource today.
// The live path is raw_capture's UVC transport (row #171 defines the seam;
// #178 builds the transport behind it). Two things this adapter will not
// invent, and each is a small interface:
//
//   1. the OB frame words the RPC array carries beside the pixels — the
//      second time word, the OBFormat and the IR light type (frame-codec
//      §10.3). raw_capture::Frame carries pixels, a timestamp and the UVC
//      format; a UVC fourcc is not the OBFormat, and a sequence number is
//      not the second time word, so a default would be a guess.
//   2. the device claim. The privileged helper (#170) owns the USB claim;
//      the shim never takes it, and a live source is only usable while the
//      helper holds it.
//
// Start() checks both and the transport's own configuration, and refuses
// with `not-configured: ...` naming every missing piece. It is a seam, not a
// device: nothing here guesses an endpoint or asserts a claim.

#include <cstdint>
#include <memory>
#include <string>

#include "frame_source.h"

namespace raw_capture {
class FrameSource;
}

namespace shim {

// The producer words of the RPC array that the raw transport does not carry.
struct ObFrameWords {
    uint64_t e2 = 0;   // the frame's second time/counter word
    uint32_t e3 = 0;   // the frame's OBFormat
    int32_t e10 = -1;  // the IR light type
};

class ObFrameWordsProvider {
public:
    virtual ~ObFrameWordsProvider() = default;
    // Fills the words for the frame just read. False means "not supplied",
    // which the adapter reports instead of substituting something.
    virtual bool Words(uint64_t seq, uint64_t timestamp_us, ObFrameWords* out) const = 0;
};

// Who holds the device. The shim asks; it never claims.
class ClaimChannel {
public:
    enum class State { kUnknown, kClaimedByHelper, kNotClaimed };
    virtual ~ClaimChannel() = default;
    virtual State Query(std::string* detail) = 0;
};

class LiveSource : public FrameSource {
public:
    // One instance per interface: a raw_capture transport reads one stream.
    LiveSource(raw_capture::FrameSource* transport, ObFrameWordsProvider* words, ClaimChannel* claim);
    ~LiveSource() override;

    bool Start(const Interface& iface, std::string* err) override;
    void Stop(const Interface& iface) override;
    bool Next(const Interface& iface, FramePayload* out, std::string* err) override;

    const char* Name() const override { return "live"; }

    // The reason Start refused, retained so a caller that keeps the object can
    // report it. Empty when the source started.
    const std::string& configuration_error() const { return error_; }

    // Take ownership of the transport (the production factory's path). The
    // raw pointer and this owner are the same object.
    void OwnTransport(std::unique_ptr<raw_capture::FrameSource> transport);

private:
    std::unique_ptr<raw_capture::FrameSource> owned_transport_;
    raw_capture::FrameSource* transport_ = nullptr;
    ObFrameWordsProvider* words_ = nullptr;
    ClaimChannel* claim_ = nullptr;
    bool started_ = false;
    std::string error_;
};

// The production transport today: raw_capture's UVC source, whose descriptor
// table is still unknown, so it refuses and names the first missing field.
// When #178's transport lands it is constructed here instead.
class LiveSourceFactory {
public:
    // Builds the live source for one interface, or returns null with the
    // refusal in `err`. The caller owns the returned object; the claim
    // channel is not owned.
    static LiveSource* Create(ClaimChannel* claim, std::string* err);
};

}  // namespace shim
