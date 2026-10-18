#pragma once

#include <cstddef>
#include <string>

namespace openscanner {

class WsBroadcastServer;

struct FrameSink {
    virtual ~FrameSink() = default;
    virtual void onBinary(const uint8_t* data, size_t size) = 0;
    virtual void onText(const std::string& text) = 0;
};

class CaptureSource {
public:
    virtual ~CaptureSource() = default;
    virtual void subscribe(FrameSink& sink) = 0;
    virtual int run() = 0;
};

} // namespace openscanner
