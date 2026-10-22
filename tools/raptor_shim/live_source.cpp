#include "live_source.h"

#include <memory>
#include <vector>

#include "ob_words.h"
#include "raw_capture.h"

namespace shim {
namespace {

// Collects the reasons Start refuses, so one line names every missing piece
// rather than only the first one found.
void AddReason(std::string* why, const std::string& reason) {
    if (!why->empty()) *why += "; ";
    *why += reason;
}

}  // namespace

LiveSource::LiveSource(raw_capture::FrameSource* transport, const ObFrameWordsProvider* words,
                       ClaimChannel* claim)
    : transport_(transport), words_(words), claim_(claim) {}

LiveSource::~LiveSource() = default;

void LiveSource::OwnTransport(std::unique_ptr<raw_capture::FrameSource> transport) {
    transport_ = transport.get();
    owned_transport_ = std::move(transport);
}

bool LiveSource::Start(const Interface& iface, std::string* err) {
    (void)iface;
    error_.clear();
    started_ = false;

    std::string why;

    // 1. The claim belongs to the helper (#170). Without it the shim must not
    //    touch the device, so the source refuses.
    if (!claim_) {
        AddReason(&why,
                  "not-configured: no claim channel (the device claim belongs to openscanner-helper, row #170)");
    } else {
        std::string detail;
        const ClaimChannel::State state = claim_->Query(&detail);
        if (state != ClaimChannel::State::kClaimedByHelper) {
            AddReason(&why, "not-configured: the device is not claimed by openscanner-helper" +
                                (detail.empty() ? std::string() : " (" + detail + ")"));
        }
    }

    // 2. The raw transport must say it is configured; its own refusal is the
    //    most specific message available.
    if (!transport_) {
        AddReason(&why, "not-configured: no live transport is linked (row #178)");
    } else if (!transport_->configured()) {
        // The transport's own message may already carry the prefix.
        const std::string transport_why = transport_->configuration_error();
        const bool prefixed = transport_why.rfind("not-configured", 0) == 0;
        AddReason(&why, (prefixed ? "" : "not-configured: ") + transport_why);
    }

    // 3. The OB words the array carries are not on the raw frame. The
    //    provider decides, and names the input it is missing.
    if (!words_) {
        AddReason(&why,
                  "not-configured: the live transport does not supply the frame's OB words (e1/e2/e3/e10)");
    } else {
        std::string words_why;
        if (!words_->Configured(iface, &words_why)) {
            AddReason(&why, words_why.empty() ? "not-configured: the OB words are not supplied" : words_why);
        }
    }

    if (!why.empty()) {
        error_ = why;
        if (err) *err = error_;
        return false;
    }
    started_ = true;
    if (err) err->clear();
    return true;
}

void LiveSource::Stop(const Interface& iface) {
    (void)iface;
    started_ = false;
}

bool LiveSource::Next(const Interface& iface, FramePayload* out, std::string* err) {
    (void)iface;
    if (err) err->clear();  // a clean end must not report the previous reason
    if (!started_) {
        if (err) *err = error_.empty() ? "live source not started" : error_;
        return false;
    }
    raw_capture::Frame frame;
    std::string why;
    if (!transport_->Next(&frame, &why)) {
        // An empty reason is the clean end of the stream; a reason is a fault.
        if (!why.empty() && err) *err = why;
        return false;
    }
    ObFrameWords words;
    if (!words_ || !words_->Words(iface, frame.seq, frame.timestamp_us, &words)) {
        if (err) *err = "not-configured: the OB words could not be supplied for this frame";
        return false;
    }
    FramePayload payload;
    // e1/e2 are the host clock domains, not the device PTS (ob_words.h).
    payload.e1_us = words.e1;
    payload.e2 = words.e2;
    payload.e3 = words.e3;
    payload.e10 = words.e10;
    payload.data = std::move(frame.data);
    // The raw capture path reads one plane; the vendor frame's sibling planes
    // are empty here rather than filled with anything made up.
    if (out) *out = std::move(payload);
    return true;
}

LiveSource* LiveSourceFactory::Create(ClaimChannel* claim, std::string* err) {
    // The shim does not link the live macOS backend yet: raw_capture's
    // UvcSource is a refusing placeholder that names the live factory, so this
    // path fails closed rather than looking live. Wiring MakeLiveUvcSource
    // into the shim is row #179's transport step.
    std::unique_ptr<raw_capture::UvcSource> transport(new raw_capture::UvcSource());
    // e1/e2 from the host clocks; e3 the observed OBFormat; e10 has no source
    // yet, so the provider refuses it by name (ob_words.h).
    static const UnwiredIrLightState kNoLightSource;
    static const HostClockObWords kWords(&SystemHostClocks(), &kNoLightSource);
    std::unique_ptr<LiveSource> source(new LiveSource(transport.get(), &kWords, claim));
    std::string why;
    if (!source->Start(IrInterface(), &why)) {
        if (err) *err = why;
        return nullptr;
    }
    // The returned source owns the transport: the raw pointer and the owner
    // are the same object.
    source->OwnTransport(std::move(transport));
    return source.release();
}

}  // namespace shim
