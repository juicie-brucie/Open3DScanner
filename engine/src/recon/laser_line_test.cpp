// laser_line_test — the device-free proof for the laser-line module (row #200).
//
// Two proofs:
//   1. synthetic ground truth: Gaussian lines at known sub-pixel positions,
//      with the measured centre error (mean/p95/max);
//   2. a metric pass over the recorded #132 IR capture
//      (--report <port-10013.bin>), with an optional PGM overlay dump.
//
// It carries its own minimal RCRP reader and msgpack bin finder, so the engine
// gains no dependency on tools/ (the format is documented in
// tools/raptor_client/capture.h). The recorded frames are our own capture;
// no vendor binary or decompiled output is read.

#include "recon/laser_line.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace recon = openscanner::recon;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::printf("[laser-line-test] %-62s %s\n", what.c_str(), condition ? "PASS" : "FAIL");
    if (!condition) ++g_failures;
}

// A deterministic 0..1 generator (no <random> dependence across platforms).
class Lcg {
public:
    explicit Lcg(uint32_t seed) : state_(seed) {}
    double Next() {
        state_ = state_ * 1664525u + 1013904223u;
        return static_cast<double>(state_ >> 8) / 16777216.0;
    }
    double Signed() { return Next() * 2.0 - 1.0; }

private:
    uint32_t state_;
};

struct Synthetic {
    std::vector<uint8_t> pixels;
    int width = 0;
    int height = 0;
    std::vector<double> true_x;  // the line's true centre in each row
};

// One Gaussian line per row at true_x[y], plus a constant background and
// optional uniform noise. Values are clamped to 0..255 and rounded.
Synthetic MakeLineFrame(int width, int height, double x0, double slope, double sigma, double amplitude,
                        double background, double noise) {
    Synthetic frame;
    frame.width = width;
    frame.height = height;
    frame.pixels.assign(static_cast<size_t>(width) * height, 0);
    frame.true_x.resize(height);
    Lcg rng(0x5eed1234u);
    // Cycle three sub-pixel phases, so the test is not accidentally aligned to
    // the pixel grid.
    const double phases[3] = {0.13, 0.47, 0.81};
    for (int y = 0; y < height; ++y) {
        const double tx = x0 + slope * y + phases[y % 3];
        frame.true_x[y] = tx;
        for (int x = 0; x < width; ++x) {
            const double dx = (x - tx) / sigma;
            double v = background + amplitude * std::exp(-0.5 * dx * dx);
            if (noise > 0.0) v += noise * rng.Signed();
            int iv = static_cast<int>(std::lround(v));
            if (iv < 0) iv = 0;
            if (iv > 255) iv = 255;
            frame.pixels[static_cast<size_t>(y) * width + x] = static_cast<uint8_t>(iv);
        }
    }
    return frame;
}

struct ErrorStats {
    int n = 0;
    double mean = 0.0, p95 = 0.0, max = 0.0;
};

ErrorStats Summarise(std::vector<double> errors) {
    ErrorStats s;
    s.n = static_cast<int>(errors.size());
    if (errors.empty()) return s;
    double sum = 0.0;
    for (double e : errors) sum += e;
    s.mean = sum / errors.size();
    std::sort(errors.begin(), errors.end());
    s.max = errors.back();
    const size_t idx = errors.size() * 95 / 100;
    s.p95 = errors[std::min(idx, errors.size() - 1)];
    return s;
}

// The measured accuracy bound over the rows of one synthetic line frame.
ErrorStats MeasureAccuracy(const Synthetic& frame, const recon::Options& options = {}) {
    recon::FrameView view{frame.pixels.data(), frame.width, frame.height, frame.width};
    const recon::Result result = recon::ExtractLaserLines(view, options);
    std::vector<double> errors;
    for (int y = 0; y < frame.height; ++y) {
        const recon::RowResult& row = result.rows[static_cast<size_t>(y)];
        if (row.centres.empty()) continue;
        double best = 1e9;
        for (const recon::LineCenter& c : row.centres) best = std::min(best, std::fabs(c.x - frame.true_x[y]));
        errors.push_back(best);
    }
    return Summarise(std::move(errors));
}

void TestSyntheticAccuracy() {
    // 1024 x 256, a line drifting across ~95 px, sigma 2.2 px, amplitude 110
    // over a background of 18, with uniform +-1.5 counts of noise.
    const Synthetic frame = MakeLineFrame(1024, 256, 400.0, 0.37, 2.2, 110.0, 18.0, 1.5);
    const ErrorStats s = MeasureAccuracy(frame);
    std::printf("[laser-line-test] synthetic accuracy: n=%d mean=%.4f p95=%.4f max=%.4f px\n", s.n, s.mean,
                s.p95, s.max);
    Check(s.n == frame.height, "the synthetic line is found in every row");
    Check(s.mean < 0.05, "mean centre error < 0.05 px");
    Check(s.p95 < 0.12, "p95 centre error < 0.12 px");
    Check(s.max < 0.30, "max centre error < 0.30 px");

    // The same measurement across widths and contrasts: the reported bound is
    // the worst row over the sweep, not one lucky profile.
    ErrorStats worst;
    for (double sigma : {1.2, 2.2, 3.2}) {
        for (double amp : {40.0, 110.0}) {
            for (double noise : {0.5, 1.5}) {
                const Synthetic f = MakeLineFrame(1024, 128, 380.0, 0.29, sigma, amp, 18.0, noise);
                const ErrorStats e = MeasureAccuracy(f);
                worst.mean = std::max(worst.mean, e.mean);
                worst.p95 = std::max(worst.p95, e.p95);
                worst.max = std::max(worst.max, e.max);
                worst.n = std::max(worst.n, e.n);
            }
        }
    }
    std::printf("[laser-line-test] sweep worst (sigma 1.2-3.2, amp 40/110, noise 0.5/1.5): "
                "n=%d mean=%.4f p95=%.4f max=%.4f px\n", worst.n, worst.mean, worst.p95, worst.max);
    Check(worst.n == 128, "the sweep's line is found in every row");
    Check(worst.mean < 0.10, "sweep worst mean centre error < 0.10 px");
    Check(worst.p95 < 0.25, "sweep worst p95 centre error < 0.25 px");
    Check(worst.max < 0.60, "sweep worst max centre error < 0.60 px");
}

void TestTwoLinesPerRow() {
    // Two lines in every row: a strong one at 200.3 + 0.1*y and a weaker one
    // at 600.7 + 0.1*y, 400 px apart (well outside each CoG window).
    const int w = 800, h = 64;
    std::vector<uint8_t> pixels(static_cast<size_t>(w) * h);
    Lcg rng(0x1234abcd);
    for (int y = 0; y < h; ++y) {
        const double a = 200.3 + 0.1 * y, b = 600.7 + 0.1 * y;
        for (int x = 0; x < w; ++x) {
            const double da = (x - a) / 1.8, db = (x - b) / 1.8;
            double v = 20.0 + 100.0 * std::exp(-0.5 * da * da) + 70.0 * std::exp(-0.5 * db * db);
            v += 1.0 * rng.Signed();
            int iv = static_cast<int>(std::lround(v));
            pixels[static_cast<size_t>(y) * w + x] = static_cast<uint8_t>(std::max(0, std::min(255, iv)));
        }
    }
    recon::FrameView view{pixels.data(), w, h, w};
    const recon::Result result = recon::ExtractLaserLines(view);
    int rows_two = 0;
    std::vector<double> err_a, err_b;
    for (int y = 0; y < h; ++y) {
        const auto& centres = result.rows[static_cast<size_t>(y)].centres;
        if (centres.size() == 2) {
            ++rows_two;
            err_a.push_back(std::fabs(centres[0].x - (200.3 + 0.1 * y)));
            err_b.push_back(std::fabs(centres[1].x - (600.7 + 0.1 * y)));
        }
    }
    const ErrorStats a = Summarise(std::move(err_a));
    const ErrorStats b = Summarise(std::move(err_b));
    std::printf("[laser-line-test] two lines: rows=%d/64 errA mean=%.4f max=%.4f errB mean=%.4f max=%.4f px\n",
                rows_two, a.mean, a.max, b.mean, b.max);
    Check(rows_two == h, "both lines are found in every row");
    Check(a.mean < 0.05 && a.max < 0.20, "the strong line's centre is within 0.20 px");
    Check(b.mean < 0.05 && b.max < 0.20, "the weak line's centre is within 0.20 px");
}

void TestNoLine() {
    std::vector<uint8_t> pixels(320 * 64, 30);
    recon::FrameView view{pixels.data(), 320, 64, 320};
    const recon::Result result = recon::ExtractLaserLines(view);
    Check(result.valid && result.centre_count == 0, "a flat frame reports no line");
    Check(result.rows_with_line == 0, "a flat frame's rows are all no-line");
}

void TestAllNoise() {
    const int w = 512, h = 128;
    std::vector<uint8_t> pixels(static_cast<size_t>(w) * h);
    Lcg rng(0xbadc0deu);
    for (auto& p : pixels) p = static_cast<uint8_t>(rng.Next() * 255.0);
    recon::FrameView view{pixels.data(), w, h, w};
    const recon::Result result = recon::ExtractLaserLines(view);
    const double false_rate = static_cast<double>(result.rows_with_line) / h;
    std::printf("[laser-line-test] all-noise: rows_with_line=%d/%d (%.2f%%)\n", result.rows_with_line, h,
                false_rate * 100.0);
    Check(false_rate < 0.01, "uniform noise produces fewer than 1% false rows");
}

void TestLowContrast() {
    // A 4-count line on a 30 background is below the 10-count floor.
    const Synthetic frame = MakeLineFrame(512, 64, 200.0, 0.0, 2.0, 4.0, 30.0, 0.5);
    recon::FrameView view{frame.pixels.data(), frame.width, frame.height, frame.width};
    const recon::Result result = recon::ExtractLaserLines(view);
    Check(result.centre_count == 0, "a line below the strength floor is rejected");
}

void TestInvalidFrame() {
    recon::FrameView view{nullptr, 1920, 1200, 1920};
    Check(!recon::ExtractLaserLines(view).valid, "a null frame is invalid");
    std::vector<uint8_t> tiny(4, 0);
    recon::FrameView small{tiny.data(), 2, 2, 2};
    Check(!recon::ExtractLaserLines(small).valid, "a frame narrower than the 3-tap kernel is invalid");
}

// ---------------------------------------------------------------------------
// The recorded-frame pass: a minimal RCRP reader and msgpack bin finder.
// ---------------------------------------------------------------------------

uint16_t Le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t Le32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
uint64_t Le64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= static_cast<uint64_t>(p[i]) << (i * 8);
    return v;
}
uint32_t Be32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

struct BundleRecord {
    std::string method;
    uint64_t msgid = 0;
    std::vector<uint8_t> bytes;
};

// The RCRP record layout is tools/raptor_client/capture.h's.
bool ReadBundle(const std::string& path, std::vector<BundleRecord>* out, std::string* err) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (err) *err = "cannot open " + path;
        return false;
    }
    uint8_t header[12];
    in.read(reinterpret_cast<char*>(header), sizeof(header));
    if (in.gcount() != static_cast<std::streamsize>(sizeof(header)) || std::memcmp(header, "RCRP", 4) != 0 ||
        Le32(header + 4) != 1) {
        if (err) *err = path + ": not an RCRP bundle";
        return false;
    }
    while (true) {
        uint8_t lenbuf[4];
        in.read(reinterpret_cast<char*>(lenbuf), sizeof(lenbuf));
        if (in.gcount() == 0) break;  // clean end
        if (in.gcount() != static_cast<std::streamsize>(sizeof(lenbuf))) {
            if (err) *err = "a truncated record length";
            return false;
        }
        const uint32_t body_len = Le32(lenbuf);
        if (body_len < 30 || body_len > (1u << 26)) {
            if (err) *err = "an implausible record length";
            return false;
        }
        std::vector<uint8_t> body(body_len);
        in.read(reinterpret_cast<char*>(body.data()), body_len);
        if (in.gcount() != static_cast<std::streamsize>(body_len)) {
            if (err) *err = "a truncated record body";
            return false;
        }
        const uint16_t method_len = Le16(body.data() + 10);
        size_t off = 12;
        if (method_len > body_len || off + method_len + 16 > body_len) {
            if (err) *err = "a record with an implausible method length";
            return false;
        }
        BundleRecord record;
        record.method.assign(reinterpret_cast<const char*>(body.data() + off), method_len);
        off += method_len;
        record.msgid = Le64(body.data() + off);
        off += 8;
        const uint64_t byte_len = Le64(body.data() + off);
        off += 8;
        if (byte_len > body_len - off) {
            if (err) *err = "a record with an implausible byte length";
            return false;
        }
        record.bytes.assign(body.begin() + static_cast<long>(off),
                            body.begin() + static_cast<long>(off + byte_len));
        out->push_back(std::move(record));
    }
    return true;
}

// The get_frame reply's 11-element array carries a Bin(2304000) at element 4.
// The envelope is our own client's; the first bin32 of that exact length is
// the frame (the marker sits in the 49-byte prefix seen in every record).
// 2,304,000 is the known full-frame size of the gap-free #132 capture
// (1920x1200 Y8, stride 1920); a live/band source would need a size-agnostic
// path, which this recorded-frame test deliberately does not implement.
const uint8_t* FindFrameBin(const std::vector<uint8_t>& reply, uint32_t* len) {
    for (size_t i = 0; i + 5 <= reply.size(); ++i) {
        if (reply[i] != 0xc6) continue;  // bin32
        const uint32_t n = Be32(reply.data() + i + 1);
        if (n == 2304000 && i + 5 + n <= reply.size()) {
            *len = n;
            return reply.data() + i + 5;
        }
    }
    return nullptr;
}

struct Track {
    int start_y = 0;
    int last_y = 0;
    int last_x = 0;
    int length = 0;  // rows that carried this line
    int gaps = 0;    // missed rows inside its span
    std::vector<std::pair<int, int>> points;  // (y, rounded x), for the dump
};

struct FrameMetrics {
    int centres = 0;
    int rows_with_line = 0;
    int rows_with_two = 0;
    int gap_rows = 0;       // rows without a line between the first and last
    int spanned_rows = 0;
    int longest_track = 0;
    int track_count = 0;
    int long_tracks = 0;     // tracks spanning >= 20 rows: a persistent line
    int long_rows = 0;       // rows covered by those tracks
    int long_gaps = 0;       // missed rows inside them (breaks)
    int short_centres = 0;   // detections in tracks shorter than 5 rows: speckle
    int strength_hist[5] = {};  // 10-20, 20-40, 40-80, 80-160, 160+
    std::vector<std::pair<int, int>> long_points;
};

FrameMetrics MeasureFrame(const recon::Result& result, int height) {
    FrameMetrics m;
    m.centres = result.centre_count;
    m.rows_with_line = result.rows_with_line;
    int first = -1, last = -1;
    std::vector<Track> tracks;
    for (int y = 0; y < height; ++y) {
        const auto& centres = result.rows[static_cast<size_t>(y)].centres;
        if (!centres.empty()) {
            if (first < 0) first = y;
            last = y;
            if (centres.size() > 1) ++m.rows_with_two;
        }
        for (const recon::LineCenter& c : centres) {
            const int bucket = c.strength < 20 ? 0 : c.strength < 40 ? 1 : c.strength < 80 ? 2
                               : c.strength < 160 ? 3 : 4;
            ++m.strength_hist[bucket];
        }
        std::vector<bool> matched(tracks.size(), false);
        for (const recon::LineCenter& c : centres) {
            int best = -1;
            int best_dx = 1 << 30;
            const int cx = static_cast<int>(std::lround(c.x));
            for (size_t t = 0; t < tracks.size(); ++t) {
                if (matched[t]) continue;
                const int dy = y - tracks[t].last_y;
                if (dy < 1 || dy > 3) continue;  // allow a two-row gap
                const int dx = std::abs(cx - tracks[t].last_x);
                if (dx > 4 * dy + 2) continue;
                if (dx < best_dx) {
                    best_dx = dx;
                    best = static_cast<int>(t);
                }
            }
            if (best >= 0) {
                matched[static_cast<size_t>(best)] = true;
                Track& tr = tracks[static_cast<size_t>(best)];
                tr.gaps += (y - tr.last_y - 1);
                tr.last_y = y;
                tr.last_x = cx;
                ++tr.length;
                tr.points.emplace_back(y, cx);
            } else {
                Track tr;
                tr.start_y = tr.last_y = y;
                tr.last_x = cx;
                tr.length = 1;
                tr.points.emplace_back(y, cx);
                tracks.push_back(std::move(tr));
                matched.push_back(true);
            }
        }
    }
    for (const Track& t : tracks) {
        ++m.track_count;
        m.longest_track = std::max(m.longest_track, t.length);
        if (t.length >= 20) {
            ++m.long_tracks;
            m.long_rows += t.length;
            m.long_gaps += t.gaps;
            m.long_points.insert(m.long_points.end(), t.points.begin(), t.points.end());
        }
        if (t.length < 5) m.short_centres += t.length;
    }
    if (first >= 0 && last >= first) {
        m.spanned_rows = last - first + 1;
        int with_line = 0;
        for (int y = first; y <= last; ++y) {
            if (!result.rows[static_cast<size_t>(y)].centres.empty()) ++with_line;
        }
        m.gap_rows = m.spanned_rows - with_line;
    }
    return m;
}

bool WriteOverlayPgm(const std::string& path, const recon::FrameView& frame,
                     const std::vector<std::pair<int, int>>& marks, std::string* err) {
    std::vector<uint8_t> out(static_cast<size_t>(frame.width) * frame.height);
    for (int y = 0; y < frame.height; ++y) {
        const uint8_t* src = frame.data + static_cast<size_t>(y) * frame.stride;
        for (int x = 0; x < frame.width; ++x) out[static_cast<size_t>(y) * frame.width + x] = src[x] / 2;
    }
    for (const auto& mark : marks) {
        const int cx = mark.second, cy = mark.first;
        for (int dy = -1; dy <= 1; ++dy) {
            const int yy = cy + dy;
            if (yy < 0 || yy >= frame.height) continue;
            for (int dx = -1; dx <= 1; ++dx) {
                const int xx = cx + dx;
                if (xx < 0 || xx >= frame.width) continue;
                out[static_cast<size_t>(yy) * frame.width + xx] = 255;
            }
        }
    }
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        if (err) *err = "cannot open " + path;
        return false;
    }
    f << "P5\n" << frame.width << " " << frame.height << "\n255\n";
    f.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
    if (!f) {
        if (err) *err = "cannot write " + path;
        return false;
    }
    return true;
}

int RunReport(const std::string& bundle, int max_frames, const std::string& dump_path, int dump_frame) {
    std::printf("== the synthetic accuracy bound ==\n");
    {
        const Synthetic frame = MakeLineFrame(1024, 256, 400.0, 0.37, 2.2, 110.0, 18.0, 1.5);
        const ErrorStats s = MeasureAccuracy(frame);
        std::printf("synthetic: n=%d mean=%.4f p95=%.4f max=%.4f px (sigma 2.2, amp 110, noise 1.5)\n", s.n,
                    s.mean, s.p95, s.max);
        // The all-noise false-line rate is the noise-rejection number.
        const int w = 512, h = 128;
        std::vector<uint8_t> noise(static_cast<size_t>(w) * h);
        Lcg rng(0xbadc0deu);
        for (auto& p : noise) p = static_cast<uint8_t>(rng.Next() * 255.0);
        recon::FrameView nv{noise.data(), w, h, w};
        const recon::Result nr = recon::ExtractLaserLines(nv);
        std::printf("uniform-noise: false rows=%d/%d (%.2f%%)\n", nr.rows_with_line, h,
                    100.0 * nr.rows_with_line / h);
    }

    std::vector<BundleRecord> records;
    std::string err;
    if (!ReadBundle(bundle, &records, &err)) {
        std::fprintf(stderr, "[laser-line-report] %s\n", err.c_str());
        return 1;
    }
    std::vector<const BundleRecord*> frames;
    for (const auto& r : records) {
        if (r.method.rfind("ob_rpc_get_frame", 0) == 0) frames.push_back(&r);
    }
    std::printf("== recorded frames ==\n");
    std::printf("bundle: %s\n", bundle.c_str());
    std::printf("records=%zu get_frame=%zu\n", records.size(), frames.size());
    if (frames.empty()) {
        std::fprintf(stderr, "[laser-line-report] no get_frame records\n");
        return 1;
    }

    const int limit = max_frames > 0 ? std::min<int>(max_frames, static_cast<int>(frames.size()))
                                     : static_cast<int>(frames.size());
    long long total_centres = 0, total_rows = 0, total_two = 0, total_gap = 0, total_span = 0;
    long long total_long_tracks = 0, total_long_rows = 0, total_long_gaps = 0, total_short = 0;
    int longest_track = 0, min_rows = 1 << 30, max_rows = 0;
    int processed = 0;
    bool dumped = false;
    for (int fi = 0; fi < limit; ++fi) {
        uint32_t frame_len = 0;
        const uint8_t* frame_data = FindFrameBin(frames[static_cast<size_t>(fi)]->bytes, &frame_len);
        if (!frame_data) {
            std::fprintf(stderr, "[laser-line-report] frame %d carries no Bin(2304000)\n", fi);
            continue;
        }
        recon::FrameView view{frame_data, 1920, 1200, 1920};
        const recon::Result result = recon::ExtractLaserLines(view);
        const FrameMetrics m = MeasureFrame(result, view.height);
        ++processed;
        total_centres += m.centres;
        total_rows += m.rows_with_line;
        total_two += m.rows_with_two;
        total_gap += m.gap_rows;
        total_span += m.spanned_rows;
        total_long_tracks += m.long_tracks;
        total_long_rows += m.long_rows;
        total_long_gaps += m.long_gaps;
        total_short += m.short_centres;
        longest_track = std::max(longest_track, m.longest_track);
        min_rows = std::min(min_rows, m.rows_with_line);
        max_rows = std::max(max_rows, m.rows_with_line);
        if (fi < 5) {
            std::printf("frame %d: centres=%d rows_with_line=%d/1200 (%.1f%%) two_lines_rows=%d "
                        "long_tracks=%d long_rows=%d longest_track=%d strength[10-20,20-40,40-80,80-160,160+]= "
                        "%d/%d/%d/%d/%d\n",
                        fi, m.centres, m.rows_with_line, 100.0 * m.rows_with_line / view.height, m.rows_with_two,
                        m.long_tracks, m.long_rows, m.longest_track, m.strength_hist[0], m.strength_hist[1],
                        m.strength_hist[2], m.strength_hist[3], m.strength_hist[4]);
        }
        if (!dump_path.empty() && fi == dump_frame && !dumped) {
            if (WriteOverlayPgm(dump_path, view, m.long_points, &err)) {
                std::printf("dump: %s (frame %d; persistent-line centres marked white, image halved)\n",
                            dump_path.c_str(), fi);
                dumped = true;
            } else {
                std::fprintf(stderr, "[laser-line-report] dump: %s\n", err.c_str());
            }
        }
    }
    if (processed == 0) {
        std::fprintf(stderr, "[laser-line-report] no frames processed\n");
        return 1;
    }
    const double rows_mean = static_cast<double>(total_rows) / processed;
    const double centres_mean = static_cast<double>(total_centres) / processed;
    const double gap_rate = total_span > 0 ? static_cast<double>(total_gap) / total_span : 0.0;
    std::printf("== aggregate over %d frame(s) ==\n", processed);
    std::printf("rows_with_line: mean=%.1f/1200 (%.1f%%) min=%d max=%d\n", rows_mean, 100.0 * rows_mean / 1200,
                min_rows, max_rows);
    std::printf("centres: mean=%.1f/frame, %.2f per row-with-line\n", centres_mean,
                total_rows > 0 ? static_cast<double>(total_centres) / total_rows : 0.0);
    std::printf("rows with two or more lines: mean=%.1f/frame\n",
                static_cast<double>(total_two) / processed);
    std::printf("continuity: raw internal gap rows=%lld/%lld (%.1f%% of spanned rows), "
                "longest track=%d rows\n",
                static_cast<long long>(total_gap), static_cast<long long>(total_span), 100.0 * gap_rate,
                longest_track);
    std::printf("persistent lines (>=20 rows): mean=%.1f tracks/frame, rows covered mean=%.1f/1200, "
                "breaks inside them=%lld rows\n",
                static_cast<double>(total_long_tracks) / processed,
                static_cast<double>(total_long_rows) / processed, static_cast<long long>(total_long_gaps));
    std::printf("persistence filter: %.1f%% of raw centres are in tracks >=20 rows; "
                "short-lived speckle (<5 rows)=%.1f centres/frame\n",
                total_centres > 0 ? 100.0 * static_cast<double>(total_long_rows) / total_centres : 0.0,
                static_cast<double>(total_short) / processed);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::string report_path, dump_path;
    int frames = 0;
    int dump_frame = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--report" && i + 1 < argc) {
            report_path = argv[++i];
        } else if (arg == "--frames" && i + 1 < argc) {
            frames = std::atoi(argv[++i]);
        } else if (arg == "--dump" && i + 1 < argc) {
            dump_path = argv[++i];
        } else if (arg == "--dump-frame" && i + 1 < argc) {
            dump_frame = std::atoi(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            std::printf("usage: laser_line_test [--report RCRP] [--frames N] [--dump out.pgm] [--dump-frame N]\n"
                        "  no arguments: the synthetic tests (ctest)\n"
                        "  --report: the recorded #132 IR frames (port-10013.bin)\n");
            return 0;
        } else {
            std::fprintf(stderr, "laser_line_test: unknown argument '%s'\n", arg.c_str());
            return 2;
        }
    }

    if (!report_path.empty()) return RunReport(report_path, frames, dump_path, dump_frame);

    TestSyntheticAccuracy();
    TestTwoLinesPerRow();
    TestNoLine();
    TestAllNoise();
    TestLowContrast();
    TestInvalidFrame();
    std::printf("[laser-line-test] %s (%d failure(s))\n", g_failures ? "FAILED" : "ok", g_failures);
    return g_failures ? 1 : 0;
}
