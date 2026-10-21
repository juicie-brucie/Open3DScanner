// Row #178: assemble UVC payloads into frames.
//
// A UVC bulk/interrupt payload starts with a header whose first byte is its
// length and whose second byte is bmHeaderInfo: bit 0 FID (toggles per
// frame), bit 1 EOF, bit 6 ERR (UVC 1.1 §2.4.3.3). A frame can span several
// payloads; the assembler joins them, drops a partial frame when the FID
// changes or the error bit is set, and bounds the buffer to the frame size
// the descriptor reports. Pure, so the live source's framing is testable.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace raw_capture {

struct UvcFrame {
    std::vector<uint8_t> data;
    int fid = 0;
};

class UvcAssembler {
public:
    explicit UvcAssembler(uint32_t max_frame_bytes) : max_frame_bytes_(max_frame_bytes) {}

    // Feeds one payload (header included). Returns true when a frame
    // completed; `frame` then holds it. On a protocol error returns false
    // with `error` set; the assembler resets and keeps going on the next call.
    bool Feed(const uint8_t* data, size_t size, UvcFrame* frame, std::string* error);

    uint64_t frames_completed() const { return completed_; }
    uint64_t frames_dropped() const { return dropped_; }
    void Reset();

private:
    void DropPartial();

    uint32_t max_frame_bytes_ = 0;
    std::vector<uint8_t> buffer_;
    int current_fid_ = -1;
    bool have_partial_ = false;
    uint64_t completed_ = 0;
    uint64_t dropped_ = 0;
};

}  // namespace raw_capture
