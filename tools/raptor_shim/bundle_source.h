#pragma once
// The offline frame source: the verified #132 capture bundles (row #172's
// scope adjustment). A bundle is the recorder's own format
// (tools/raptor_client/capture.h), read here with that tool's Reader so there
// is one parser for it, not two.
//
// The source streams: it keeps no frames in memory, only the reader's
// position. At the end of a bundle it reopens and cycles, so a multi-second
// client run keeps pulling frames from a finite capture; each wrap is logged.

#include <cstdint>
#include <memory>
#include <string>

#include "frame_source.h"

namespace capture {
class Reader;
}

namespace shim {

class BundleSource : public FrameSource {
public:
    BundleSource();
    ~BundleSource() override;

    // Opens the bundle and checks that it carries this interface's get_frame
    // replies. The bundle directory is the recorder's root (it contains
    // replies/port-<port>.bin).
    bool Open(const std::string& bundle_dir, const Interface& iface, std::string* err);

    bool Start(const Interface& iface, std::string* err) override;
    void Stop(const Interface& iface) override;
    bool Next(const Interface& iface, FramePayload* out, std::string* err) override;

    const char* Name() const override { return "bundle"; }

    uint64_t frames_served() const { return served_; }
    uint64_t wraps() const { return wraps_; }
    uint64_t skipped() const { return skipped_; }

private:
    bool OpenReader(std::string* err);

    std::string dir_;
    Interface iface_;
    std::unique_ptr<capture::Reader> reader_;
    uint64_t served_ = 0;
    uint64_t wraps_ = 0;
    uint64_t skipped_ = 0;
    bool saw_frame_ = false;
    bool started_ = false;
};

}  // namespace shim
