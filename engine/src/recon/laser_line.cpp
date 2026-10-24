#include "recon/laser_line.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace openscanner::recon {
namespace {

// The row's background: the median of its 8-bit values. A laser line is a
// narrow feature, so the median sits in the scene/background, not on the line.
// A 256-bin histogram makes this O(width) per row instead of a sort.
int RowMedian(const uint8_t* row, int width) {
    int hist[256] = {};
    for (int i = 0; i < width; ++i) { ++hist[row[i]]; }
    const int half = width / 2;
    int cumulative = 0;
    for (int v = 0; v < 256; ++v) {
        cumulative += hist[v];
        if (cumulative > half) return v;
    }
    return 255;
}

// 1.4826 * the median absolute deviation: a robust noise scale for the row.
float RowNoise(const float* values, int width) {
    int hist[257] = {};  // the last bin is ">= 255"
    for (int i = 0; i < width; ++i) {
        int v = static_cast<int>(std::fabs(values[i]));
        if (v > 255) v = 256;
        ++hist[v];
    }
    const int half = width / 2;
    int cumulative = 0;
    int mad = 0;
    for (int v = 0; v < 257; ++v) {
        cumulative += hist[v];
        if (cumulative > half) {
            mad = v;
            break;
        }
    }
    // 1.4826 * median(|d|) is a true MAD only when median(d) ~= 0. Here the
    // background is the raw-row median while d uses the smoothed row, so this
    // is a slight (conservative) over-estimate of the noise; kept deliberately.
    return 1.4826f * static_cast<float>(mad);
}

}  // namespace

Result ExtractLaserLines(const FrameView& frame, const Options& options) {
    Result result;
    if (!frame.data || frame.width <= 2 || frame.height <= 0 || frame.stride < frame.width) {
        return result;
    }
    result.valid = true;
    result.width = frame.width;
    result.height = frame.height;
    result.rows.resize(static_cast<size_t>(frame.height));

    const int width = frame.width;
    std::vector<float> smooth(static_cast<size_t>(width));
    std::vector<float> d(static_cast<size_t>(width));
    std::vector<int> peaks;
    std::vector<int> kept;
    peaks.reserve(64);
    kept.reserve(64);

    for (int y = 0; y < frame.height; ++y) {
        const uint8_t* row = frame.data + static_cast<size_t>(y) * static_cast<size_t>(frame.stride);
        const float background = static_cast<float>(RowMedian(row, width));

        // A 3-tap [1,2,1]/4 smooth: enough to suppress single-pixel shot noise
        // without moving a symmetric line's centre.
        for (int i = 0; i < width; ++i) {
            const float left = row[i > 0 ? i - 1 : 0];
            const float centre = row[i];
            const float right = row[i + 1 < width ? i + 1 : width - 1];
            smooth[i] = (left + 2.0f * centre + right) * 0.25f;
            d[i] = smooth[i] - background;
        }

        const float noise = RowNoise(d.data(), width);
        const float threshold = std::max(options.min_strength, options.min_snr * noise);

        peaks.clear();
        for (int i = 1; i + 1 < width; ++i) {
            if (d[i] >= d[i - 1] && d[i] > d[i + 1] && d[i] >= threshold) {
                peaks.push_back(i);
            }
        }
        if (peaks.empty()) continue;

        // Non-maximum suppression: the strongest peak in a neighbourhood wins.
        std::sort(peaks.begin(), peaks.end(), [&d](int a, int b) { return d[a] > d[b]; });
        kept.clear();
        for (int peak : peaks) {
            bool close = false;
            for (int other : kept) {
                if (std::abs(peak - other) < options.min_peak_distance) {
                    close = true;
                    break;
                }
            }
            if (!close) kept.push_back(peak);
            if (static_cast<int>(kept.size()) >= options.max_lines_per_row) break;
        }
        std::sort(kept.begin(), kept.end());

        RowResult& out = result.rows[static_cast<size_t>(y)];
        out.centres.reserve(kept.size());
        for (size_t k = 0; k < kept.size(); ++k) {
            const int peak = kept[k];
            // The CoG window stops at the midpoint to a neighbouring kept
            // peak, so a second line in the row cannot pull this centre.
            int left = peak - options.window_radius;
            int right = peak + options.window_radius;
            if (k > 0) left = std::max(left, (kept[k - 1] + peak) / 2 + 1);
            if (k + 1 < kept.size()) right = std::min(right, (peak + kept[k + 1]) / 2);
            left = std::max(left, 0);
            right = std::min(right, width - 1);

            double sum = 0.0;
            double sum_x = 0.0;
            for (int i = left; i <= right; ++i) {
                const double w = std::max(0.0f, d[i]);
                sum += w;
                sum_x += w * i;
            }
            if (sum <= 0.0) continue;
            const double cx = sum_x / sum;

            double moment = 0.0;
            for (int i = left; i <= right; ++i) {
                const double w = std::max(0.0f, d[i]);
                const double dx = i - cx;
                moment += w * dx * dx;
            }
            const double sigma = std::sqrt(moment / sum);
            // Reject blobs (a dot or lamp is wider than a laser line) and
            // sub-pixel-width noise.
            if (sigma < options.min_width || sigma > options.max_width) continue;

            LineCenter centre;
            centre.y = y;
            centre.x = static_cast<float>(cx);
            centre.strength = d[peak];
            centre.confidence = d[peak] / (d[peak] + noise + 1e-3f);
            if (centre.confidence > 1.0f) centre.confidence = 1.0f;
            centre.width = static_cast<float>(sigma);
            out.centres.push_back(centre);
        }
        if (!out.centres.empty()) {
            out.status = RowStatus::kFound;
            ++result.rows_with_line;
            result.centre_count += static_cast<int>(out.centres.size());
        }
    }
    return result;
}

}  // namespace openscanner::recon
