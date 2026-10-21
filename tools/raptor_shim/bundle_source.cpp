#include "bundle_source.h"

#include <cstdio>

#include "../raptor_client/capture.h"
#include "msgpack.h"

namespace shim {

BundleSource::BundleSource() = default;
BundleSource::~BundleSource() = default;

bool BundleSource::Open(const std::string& bundle_dir, const Interface& iface, std::string* err) {
    dir_ = bundle_dir;
    iface_ = iface;
    return OpenReader(err);
}

bool BundleSource::OpenReader(std::string* err) {
    reader_.reset(new capture::Reader());
    std::string why;
    if (!reader_->Open(dir_, &why)) {
        if (err) *err = why;
        reader_.reset();
        return false;
    }
    return true;
}

bool BundleSource::Start(const Interface& iface, std::string* err) {
    if (iface.suffix != iface_.suffix) {
        if (err) *err = "this bundle serves " + iface_.name + ", not " + iface.name;
        return false;
    }
    if (!reader_) {
        if (!OpenReader(err)) return false;
    }
    started_ = true;
    return true;
}

void BundleSource::Stop(const Interface& iface) {
    (void)iface;
    started_ = false;
}

bool BundleSource::Next(const Interface& iface, FramePayload* out, std::string* err) {
    if (iface.suffix != iface_.suffix) {
        if (err) *err = "this bundle serves " + iface_.name + ", not " + iface.name;
        return false;
    }
    // A full pass with no frame is a bundle that cannot serve this interface:
    // say so instead of cycling forever.
    bool passed_end = false;
    for (;;) {
        capture::Record record;
        if (!reader_ || !reader_->Next(&record)) {
            if (!reader_) return false;
            if (passed_end && !saw_frame_) {
                if (err) *err = "no get_frame replies for " + iface_.name + " in " + dir_;
                return false;
            }
            passed_end = true;
            ++wraps_;
            std::string why;
            if (!OpenReader(&why)) {
                if (err) *err = why;
                return false;
            }
            continue;
        }
        if (record.port != iface_.port) continue;
        if (record.method.find("ob_rpc_get_frame") != 0) continue;
        if (record.method.size() >= iface_.suffix.size() &&
            record.method.compare(record.method.size() - iface_.suffix.size(), iface_.suffix.size(),
                                  iface_.suffix) != 0) {
            continue;
        }
        Value envelope;
        if (!ParseValue(record.bytes.data(), record.bytes.size(), &envelope) || !envelope.is_array(4)) {
            ++skipped_;
            continue;
        }
        FramePayload frame;
        if (!ParseFrameArray(envelope.array[3], &frame)) {
            ++skipped_;
            continue;
        }
        saw_frame_ = true;
        ++served_;
        if (out) *out = std::move(frame);
        return true;
    }
}

}  // namespace shim
