#pragma once
// Laser-line extraction from one IR frame (row #200, Phase C seed).
//
// The input is the recorded RPC IR frame: 1920x1200, one byte per pixel, row
// stride 1920 (docs/recon/findings/frame-geometry.md §2). The output is the
// sub-pixel centre of each laser line in each row: a row-wise
// centre-of-gravity (CoG) over a background-subtracted profile, the textbook
// laser-triangulation measurement (Steger's ridge detector is the 2-D
// alternative; the row-wise CoG is used here because every line in the IR
// frames crosses a row transversally and the CoG is the least biased
// estimator for a symmetric profile).
//
// This module is measurement only: no calibration, no triangulation, no 3-D,
// no vendor ABI. The options are deliberately few.

#include <cstdint>
#include <vector>

namespace openscanner::recon {

// A caller-owned view of one 8-bit image. `stride` is bytes per row (>= width).
struct FrameView {
    const uint8_t* data = nullptr;
    int width = 0;
    int height = 0;
    int stride = 0;
};

// One sub-pixel line centre in one row.
struct LineCenter {
    int y = 0;              // row index
    float x = 0.0f;         // sub-pixel column, 0 .. width-1
    float strength = 0.0f;  // peak brightness above the row background (0..255)
    float confidence = 0.0f;// strength / (strength + the row's noise), 0..1
    float width = 0.0f;     // CoG second moment, pixels (a Gaussian line's sigma)
};

enum class RowStatus : uint8_t {
    kNoLine = 0,
    kFound = 1,
};

struct RowResult {
    RowStatus status = RowStatus::kNoLine;
    std::vector<LineCenter> centres;  // ordered by x
};

struct Options {
    // A peak must clear an absolute floor and the row's own noise.
    float min_strength = 10.0f;  // peak - row background, in pixel counts
    float min_snr = 3.0f;        // peak / (1.4826 * MAD of the row)

    // A laser line is narrow; a saturated blob (a retro-reflective dot, a
    // lamp) is not a line. The width is the CoG second moment in pixels.
    float min_width = 0.3f;
    float max_width = 4.0f;

    // Peaks closer than this are one line; a row can carry several lines.
    int min_peak_distance = 5;
    int max_lines_per_row = 4;

    // CoG window: the midpoint to the neighbouring peak, capped at this radius.
    int window_radius = 7;
};

struct Result {
    bool valid = false;  // false for a null/undersized frame
    int width = 0;
    int height = 0;
    std::vector<RowResult> rows;
    int rows_with_line = 0;
    int centre_count = 0;
};

// Extract the laser-line centres of one frame. Allocation is bounded to the
// result and a few row-sized scratch buffers; the call is deterministic.
Result ExtractLaserLines(const FrameView& frame, const Options& options = {});

}  // namespace openscanner::recon
