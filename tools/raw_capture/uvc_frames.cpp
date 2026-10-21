// The UVC payload assembler (row #178). See uvc_frames.h.

#include "uvc_frames.h"

namespace raw_capture {

void UvcAssembler::Reset() {
    buffer_.clear();
    current_fid_ = -1;
    have_partial_ = false;
}

void UvcAssembler::DropPartial() {
    if (have_partial_) ++dropped_;
    Reset();
}

bool UvcAssembler::Feed(const uint8_t* data, size_t size, UvcFrame* frame, std::string* error) {
    error->clear();
    if (data == nullptr || size < 2) {
        *error = "a UVC payload is shorter than its 2-byte header";
        DropPartial();
        return false;
    }
    const size_t header_length = data[0];
    if (header_length < 2 || header_length > size) {
        *error = "a UVC payload's bHeaderLength (" + std::to_string(header_length) +
                 ") is outside the payload (" + std::to_string(size) + " bytes)";
        DropPartial();
        return false;
    }
    const uint8_t info = data[1];
    const int fid = info & 0x01;
    const bool eof = (info & 0x02) != 0;
    const bool error_bit = (info & 0x40) != 0;
    if (error_bit) {
        // The device marked this frame as damaged: drop it, do not emit. A
        // damaged payload with no partial frame is still one dropped frame.
        const bool had_partial = have_partial_;
        DropPartial();
        if (!had_partial) ++dropped_;
        return false;
    }
    if (have_partial_ && fid != current_fid_) {
        // A new frame started before the old one ended.
        DropPartial();
    }
    if (!have_partial_) {
        have_partial_ = true;
        current_fid_ = fid;
    }
    const size_t payload = size - header_length;
    if (max_frame_bytes_ > 0 && buffer_.size() + payload > max_frame_bytes_) {
        *error = "a frame exceeded the descriptor's maximum frame size (" +
                 std::to_string(max_frame_bytes_) + " bytes)";
        DropPartial();
        return false;
    }
    buffer_.insert(buffer_.end(), data + header_length, data + size);

    // Some devices set EOF on the last payload; others rely on the buffer
    // reaching the descriptor's frame size. Either completes the frame.
    if (eof || (max_frame_bytes_ > 0 && buffer_.size() >= max_frame_bytes_)) {
        frame->data.swap(buffer_);
        frame->fid = current_fid_;
        buffer_.clear();
        have_partial_ = false;
        current_fid_ = -1;
        ++completed_;
        return true;
    }
    return false;
}

}  // namespace raw_capture
