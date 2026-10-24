// laser_line_test — the device-free synthetic proof for the laser-line module
// (row #200; split from the recorded-frame tool in row #205).
//
// Gaussian lines at known sub-pixel positions, with the measured centre error
// (mean/p95/max), plus the no-line / all-noise / two-line / low-contrast /
// invalid-frame cases. No capture, no device, no I/O beyond stdout; the
// recorded-frame pass is tools/laser_line (it uses the shared RCRP Reader).

#include "recon/laser_line.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
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

}  // namespace

int main() {
    TestSyntheticAccuracy();
    TestTwoLinesPerRow();
    TestNoLine();
    TestAllNoise();
    TestLowContrast();
    TestInvalidFrame();
    std::printf("[laser-line-test] %s (%d failure(s))\n", g_failures ? "FAILED" : "ok", g_failures);
    return g_failures ? 1 : 0;
}
