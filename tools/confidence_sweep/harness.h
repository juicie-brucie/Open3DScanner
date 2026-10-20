#pragma once

// The confidence/retention experiment's offline harness (row #159).
//
// The harness is ours end to end: it reads a cloud, scores every point
// against a ground-truth surface, estimates a per-point confidence, sweeps a
// retention threshold, and reports the accuracy of what was kept next to the
// accuracy of an equal-sized RANDOM keep. The last part is the experiment:
// a confidence signal only counts if it beats random retention at the same
// retained fraction.
//
// The estimator choice, its assumptions and the conditions that would
// falsify it are written up in
// docs/recomp/log/2026-10-20-row-159-confidence-experiment.md.
//
// Clean-room: nothing here touches the vendor ABI. The only vendor-shaped
// input is a recorded V1 session, and its payload is decoded with the
// project's own envelope codec (engine/include/openscanner/frame_protocol.h).

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace openscanner {
namespace confidence {

// ------------------------------------------------------------------ cloud --

// A point cloud: positions only, in the cloud's own frame. The vendor's
// fused cloud carries colours and normals too, but the experiment scores
// geometry, so positions are what the harness consumes.
struct Cloud {
    std::vector<float> xyz;  // 3 floats per point
    [[nodiscard]] std::size_t size() const { return xyz.size() / 3; }
    [[nodiscard]] const float* point(std::size_t i) const { return &xyz[i * 3]; }
};

// Where a cloud came from, for the run's report.
enum class CloudSource { kPly, kRec, kSynthetic };

// How a recorded session (.rec) is turned into one cloud.
enum class RecMode {
    kLastFrame,  // the last cloud frame in the recording (the fused cloud at the end)
    kUnion,      // every cloud frame's points concatenated (points counted once per frame)
    kFrame,      // one cloud frame, 0-based
};

// ------------------------------------------------------------------ truth --

// The ground truth every point is scored against: the unsigned distance from
// the point to the truth surface.
//
// A plane covers #126's flat wall (the tape-measured distance is the plane's
// offset); a sphere covers a ball or a rounded target; a point set covers an
// arbitrary surface (a checkerboard's corners, a box's faces) at the cost of a
// nearest-neighbour lookup.
class Truth {
public:
    enum class Kind { kPlane, kSphere, kPointSet };

    static Truth plane(double nx, double ny, double nz, double d);
    static Truth sphere(double cx, double cy, double cz, double r);
    static Truth point_set(std::vector<float> xyz);

    [[nodiscard]] Kind kind() const { return kind_; }
    [[nodiscard]] double distance(double x, double y, double z) const;
    [[nodiscard]] std::string describe() const;

private:
    Kind kind_ = Kind::kPlane;
    // plane: unit normal + offset along it
    double nx_ = 0.0, ny_ = 0.0, nz_ = 1.0, d_ = 0.0;
    // sphere
    double cx_ = 0.0, cy_ = 0.0, cz_ = 0.0, r_ = 1.0;
    // point set + a uniform grid over it for the nearest-neighbour lookup
    std::vector<float> pts_;
    double cell_ = 0.0;
    double ox_ = 0.0, oy_ = 0.0, oz_ = 0.0;
    int gnx_ = 0, gny_ = 0, gnz_ = 0;
    std::vector<int> cell_start_;
    std::vector<int> items_;

    void build_grid();
};

// -------------------------------------------------------------- estimator --

// Options for the local-residual estimator.
struct ResidualOptions {
    int k = 16;                     // neighbours in the local fit (including the point)
    double max_radius_factor = 24.0;  // search radius = factor * the median spacing
    int spacing_samples = 256;      // points sampled to estimate the spacing
};

// The per-point local planarity residual, in metres: the RMS distance of a
// point's k nearest neighbours to their own best-fit plane. A point sitting in
// a smooth, well-registered neighbourhood scores low; a point in a rough or
// mismatched one scores high.
//
// This is the estimator row #159 chose (option 1 in the design note); the
// other two options it weighed are recorded there.
[[nodiscard]] std::vector<float> local_residual(const Cloud& cloud, const ResidualOptions& options,
                                                 std::string* error);

// The median nearest-neighbour distance of the cloud - the "spacing" the
// neighbourhood search is sized against.
[[nodiscard]] double estimate_spacing(const Cloud& cloud, int samples);

// ------------------------------------------------------------------ sweep --

// One row of the sweep: a threshold, what it retained, and the accuracy of
// what it retained - for the confidence signal and for the random control.
struct SweepRow {
    double threshold = 0.0;          // the confidence threshold
    double retained_fraction = 0.0;  // kept / total
    std::size_t n_retained = 0;
    double rmse = 0.0;       // the confidence curve
    double p95 = 0.0;
    double median = 0.0;
    double rmse_random_mean = 0.0;  // the random control, averaged over the trials
    double rmse_random_std = 0.0;
    double p95_random_mean = 0.0;
    double median_random_mean = 0.0;
};

// The sweep's verdict: does the confidence signal beat random retention?
struct SweepSummary {
    std::size_t rows_evaluated = 0;      // rows in the acceptance window
    std::size_t rows_beaten = 0;         // rows where the confidence RMSE < the random mean
    double beats_random_fraction = 0.0;  // rows_beaten / rows_evaluated
    double mean_relative_improvement = 0.0;  // mean over the window of (rand - conf) / rand
    double min_relative_improvement = 0.0;   // the worst row's relative improvement
    double area_gap = 0.0;  // the integral of (rand - conf) over the retained fraction
    bool beats_random = false;
};

// The sweep's shape.
struct SweepOptions {
    int steps = 21;              // rows in the sweep
    bool absolute = false;       // sweep a raw confidence value instead of a quantile
    double absolute_min = 0.0;
    double absolute_max = 0.0;
    int random_trials = 20;      // random keeps averaged per row
    uint64_t seed = 20261020;    // the control's seed - fixed, so the run repeats
    // The acceptance window, in retained fraction. The full-retention row is
    // outside it by construction: there both curves are the same set of
    // points, so neither can beat the other.
    double window_lo = 0.0;
    double window_hi = 1.0;
};

// Runs the sweep. `confidence` is one value per point, higher = more trusted.
// `errors` holds the point-to-truth distance of every point.
[[nodiscard]] SweepSummary run_sweep(const std::vector<float>& confidence, const std::vector<double>& errors,
                                      const SweepOptions& options, std::vector<SweepRow>* rows);

// The acceptance: the confidence curve must beat random retention at every
// row of the window, and its mean relative improvement must clear
// `min_improvement`. Returns false when the signal does not count.
[[nodiscard]] bool meets_acceptance(const SweepSummary& summary, double min_improvement);

// --------------------------------------------------------------- reading --

// Reads a PLY cloud (ascii, binary_little_endian, binary_big_endian). Only the
// vertex element's x/y/z are read; other elements and properties are skipped.
[[nodiscard]] bool read_ply(const std::string& path, Cloud* out, std::string* error);

// Reads a recorded V1 session (the container scan_server's --record writes)
// and reduces it to one cloud. The envelope is decoded with the project's own
// codec; the container is the recorder's own.
[[nodiscard]] bool read_record(const std::string& path, RecMode mode, int frame_index, Cloud* out,
                                std::string* error);

// Reads a per-point confidence field: one "index,value" or "value" per line,
// in point order. Higher means more trusted.
[[nodiscard]] bool read_confidence_field(const std::string& path, std::size_t expected_points,
                                          std::vector<float>* out, std::string* error);

// Writes a PLY cloud (ascii) - used by the self-test and by the #126 run to
// hand a cloud to other tools.
[[nodiscard]] bool write_ply_ascii(const std::string& path, const Cloud& cloud, std::string* error);

// -------------------------------------------------------------- synthetic --

// The self-test scene: a flat plane with a clean region and a region that is
// both biased and noisy. The truth is the plane itself.
struct SyntheticSpec {
    int grid = 160;              // points per side
    double spacing = 0.004;      // metres between points
    double good_sigma = 0.00015; // clean region's noise (metres)
    double bad_sigma = 0.002;    // bad region's noise (metres)
    double bad_bias = 0.0015;    // bad region's offset along the truth's normal (metres)
    uint64_t seed = 20261020;
};

[[nodiscard]] Cloud make_synthetic_scene(const SyntheticSpec& spec, Truth* truth);

// The truth of the scene `make_synthetic_scene` builds: the plane z = 0.
[[nodiscard]] Truth synthetic_truth();

// -------------------------------------------------------------- plotting --

// Writes the sweep as an SVG: accuracy (RMSE and p95) against the retained
// fraction, the confidence curve next to the random control's mean and its
// +/- 1 standard deviation band.
[[nodiscard]] bool write_svg(const std::string& path, const std::vector<SweepRow>& rows,
                             const std::string& title, const std::string& subtitle, std::string* error);

}  // namespace confidence
}  // namespace openscanner
