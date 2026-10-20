// The confidence/retention harness's self-test (row #159).
//
// The point of the test: on a scene with known truth, a confidence signal that
// works must beat random retention at the same retained fraction - and a
// signal that does not work must not. The second half is what makes the
// first half mean anything: without it, a harness that always reports a
// winner would pass.
//
// Device-free and deterministic: it runs under `ctest -LE hardware`.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <limits>
#include <string>
#include <vector>

#include "harness.h"
#include "openscanner/frame_protocol.h"

namespace {

int g_failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "[ ok ]" : "[FAIL]", what.c_str());
    if (!ok) ++g_failures;
}

// Writes a recorded session in the container the recorder writes: a 16-byte
// header, the frames, then the terminator (see the recorder in
// tools/scan_server/main.cpp). The frames themselves are the project's own
// V1 envelope.
bool write_record(const std::string& path, const std::vector<std::vector<float>>& frames) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write("OSCR", 4);
    const uint32_t version = 1;
    out.write(reinterpret_cast<const char*>(&version), 4);
    const uint64_t count = static_cast<uint64_t>(frames.size());
    out.write(reinterpret_cast<const char*>(&count), 8);
    for (std::size_t i = 0; i < frames.size(); ++i) {
        const std::vector<uint8_t> frame = openscanner::frame_protocol::encode_cloud_snapshot(
            static_cast<uint32_t>(i), 0.0, 0.0, frames[i].data(), static_cast<uint32_t>(frames[i].size() / 3));
        out.write(reinterpret_cast<const char*>(frame.data()), static_cast<std::streamsize>(frame.size()));
    }
    out.write("OSCE", 4);
    return static_cast<bool>(out);
}

// A local generator for the negative control's shuffle - the harness's own
// generator is not part of its interface.
class Lcg {
public:
    explicit Lcg(uint64_t seed) : state_(seed * 6364136223846793005ULL + 1442695040888963407ULL) {}
    std::size_t below(std::size_t bound) {
        state_ = state_ * 6364136223846793005ULL + 1442695040888963407ULL;
        return bound ? static_cast<std::size_t>((state_ >> 33) % bound) : 0;
    }

private:
    uint64_t state_;
};

// Writes a binary PLY in the given byte order - the vendor's own model output
// is a PLY, and the binary path has to read it.
bool write_binary_ply(const std::string& path, const openscanner::confidence::Cloud& cloud, bool big_endian) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << "ply\nformat " << (big_endian ? "binary_big_endian" : "binary_little_endian") << " 1.0\n";
    out << "element vertex " << cloud.size() << "\n";
    out << "property float x\nproperty float y\nproperty float z\nend_header\n";
    auto put_f32 = [&](float v) {
        uint32_t bits;
        std::memcpy(&bits, &v, 4);
        uint8_t bytes[4] = {static_cast<uint8_t>(bits & 0xff), static_cast<uint8_t>((bits >> 8) & 0xff),
                            static_cast<uint8_t>((bits >> 16) & 0xff), static_cast<uint8_t>((bits >> 24) & 0xff)};
        if (big_endian) std::reverse(bytes, bytes + 4);
        out.write(reinterpret_cast<const char*>(bytes), 4);
    };
    for (std::size_t i = 0; i < cloud.size(); ++i) {
        const float* p = cloud.point(i);
        put_f32(p[0]);
        put_f32(p[1]);
        put_f32(p[2]);
    }
    return static_cast<bool>(out);
}

}  // namespace

int main() {
    using namespace openscanner::confidence;

    // ---- the scene -------------------------------------------------------
    SyntheticSpec spec;
    Truth truth;
    const Cloud cloud = make_synthetic_scene(spec, &truth);
    check(cloud.size() > 1000, "the synthetic scene has points");
    std::printf("       scene: %zu points, truth %s\n", cloud.size(), truth.describe().c_str());

    // ---- the errors and the estimator ------------------------------------
    std::vector<double> errors(cloud.size());
    for (std::size_t i = 0; i < cloud.size(); ++i) {
        const float* p = cloud.point(i);
        errors[i] = truth.distance(p[0], p[1], p[2]);
    }
    // The scene's bad region is the x > 0, y > 0 quadrant (see
    // make_synthetic_scene); the clean region is everything else.
    std::vector<std::size_t> bad;
    std::vector<std::size_t> good;
    for (std::size_t i = 0; i < cloud.size(); ++i) {
        const float* p = cloud.point(i);
        if (p[0] > 0.0f && p[1] > 0.0f) {
            bad.push_back(i);
        } else {
            good.push_back(i);
        }
    }
    check(bad.size() > 100 && good.size() > 100, "the scene has both a clean and a bad region");

    auto mean_of = [](const std::vector<double>& v) {
        double sum = 0.0;
        for (double x : v) sum += x;
        return v.empty() ? 0.0 : sum / static_cast<double>(v.size());
    };
    std::vector<double> bad_errors, good_errors;
    for (std::size_t i : bad) bad_errors.push_back(errors[i]);
    for (std::size_t i : good) good_errors.push_back(errors[i]);
    const double bad_rmse = std::sqrt(mean_of([&] {
        std::vector<double> sq;
        for (double e : bad_errors) sq.push_back(e * e);
        return sq;
    }()));
    const double good_rmse = std::sqrt(mean_of([&] {
        std::vector<double> sq;
        for (double e : good_errors) sq.push_back(e * e);
        return sq;
    }()));
    check(bad_rmse > 5.0 * good_rmse, "the bad region is much less accurate than the clean one");
    std::printf("       region RMSE: clean %.6g m, bad %.6g m\n", good_rmse, bad_rmse);

    ResidualOptions residual_options;
    const std::vector<float> residual = local_residual(cloud, residual_options, nullptr);
    check(residual.size() == cloud.size(), "the estimator scored every point");
    double bad_res_sum = 0.0, good_res_sum = 0.0;
    for (std::size_t i : bad) bad_res_sum += residual[i];
    for (std::size_t i : good) good_res_sum += residual[i];
    const double bad_res = bad_res_sum / static_cast<double>(bad.size());
    const double good_res = good_res_sum / static_cast<double>(good.size());
    check(bad_res > 5.0 * good_res, "the estimator's residual separates the bad region from the clean one");
    std::printf("       residual: clean %.6g m, bad %.6g m\n", good_res, bad_res);

    // ---- the sweep -------------------------------------------------------
    SweepOptions sweep;
    std::vector<float> confidence(residual.size());
    for (std::size_t i = 0; i < residual.size(); ++i) confidence[i] = -residual[i];

    std::vector<SweepRow> rows;
    const SweepSummary summary = run_sweep(confidence, errors, sweep, &rows);
    check(summary.rows_evaluated > 0, "the sweep produced rows");
    check(summary.beats_random, "the confidence curve beats random retention at every row of the window");
    std::printf("       beats random at %zu of %zu rows, mean improvement %.1f%%\n", summary.rows_beaten,
                summary.rows_evaluated, 100.0 * summary.mean_relative_improvement);
    check(summary.mean_relative_improvement > 0.5, "the confidence curve's mean improvement over random is large");
    check(meets_acceptance(summary, 0.1), "the acceptance is met");

    // Around half the points kept, the confidence curve must be far better
    // than the random control - the headline number of the experiment.
    {
        const SweepRow* nearest = nullptr;
        for (const SweepRow& r : rows) {
            if (!nearest || std::fabs(r.retained_fraction - 0.5) < std::fabs(nearest->retained_fraction - 0.5)) {
                nearest = &r;
            }
        }
        check(nearest != nullptr, "the sweep has a row near half retention");
        if (nearest) {
            char buf[256];
            std::snprintf(buf, sizeof(buf), "at %.1f%% retained: confidence RMSE %.6g m vs random %.6g m",
                          100.0 * nearest->retained_fraction, nearest->rmse, nearest->rmse_random_mean);
            std::printf("       %s\n", buf);
            check(nearest->rmse < 0.5 * nearest->rmse_random_mean,
                  "near half retention the confidence curve is far better than random");
        }
    }

    // ---- the negative control -------------------------------------------
    // A shuffled confidence field carries no information, so its curve must
    // land on the random control's curve. If it did not, the harness would be
    // reporting a winner for any field at all.
    std::vector<float> shuffled = confidence;
    {
        Lcg rng(7);
        for (std::size_t i = shuffled.size(); i-- > 1;) {
            const std::size_t j = rng.below(i + 1);
            std::swap(shuffled[i], shuffled[j]);
        }
    }
    std::vector<SweepRow> shuffled_rows;
    const SweepSummary shuffled_summary = run_sweep(shuffled, errors, sweep, &shuffled_rows);
    check(!shuffled_summary.beats_random, "a shuffled confidence field does not beat random retention");
    {
        const SweepRow* nearest = nullptr;
        for (const SweepRow& r : shuffled_rows) {
            if (!nearest || std::fabs(r.retained_fraction - 0.5) < std::fabs(nearest->retained_fraction - 0.5)) {
                nearest = &r;
            }
        }
        if (nearest) {
            char buf[256];
            std::snprintf(buf, sizeof(buf), "shuffled field at %.1f%%: RMSE %.6g m vs the control's %.6g m",
                          100.0 * nearest->retained_fraction, nearest->rmse, nearest->rmse_random_mean);
            std::printf("       %s\n", buf);
            check(nearest->rmse > 0.5 * nearest->rmse_random_mean,
                  "the shuffled field's curve sits on the random control's curve");
        }
    }

    // ---- determinism -----------------------------------------------------
    std::vector<SweepRow> rows_again;
    const SweepSummary again = run_sweep(confidence, errors, sweep, &rows_again);
    check(again.rows_beaten == summary.rows_beaten, "the same input produces the same sweep");
    bool identical = rows.size() == rows_again.size();
    for (std::size_t i = 0; identical && i < rows.size(); ++i) {
        identical = rows[i].rmse == rows_again[i].rmse && rows[i].n_retained == rows_again[i].n_retained &&
                    rows[i].rmse_random_mean == rows_again[i].rmse_random_mean;
    }
    check(identical, "the same input produces the same numbers");

    // ---- the PLY round trip ----------------------------------------------
    {
        const std::string path = "confidence_sweep_selftest.ply";
        std::string error;
        check(write_ply_ascii(path, cloud, &error), "the scene writes as a PLY");
        Cloud reread;
        check(read_ply(path, &reread, &error), "the PLY reads back");
        check(reread.size() == cloud.size(), "the PLY round trip keeps every point");
        double worst = 0.0;
        for (std::size_t i = 0; i < cloud.size() && i < reread.size(); ++i) {
            for (int a = 0; a < 3; ++a) {
                worst = std::max(worst, std::fabs(static_cast<double>(cloud.point(i)[a]) -
                                                  static_cast<double>(reread.point(i)[a])));
            }
        }
        check(worst < 1e-6, "the PLY round trip keeps the positions");
        std::remove(path.c_str());
    }

    // ---- the binary PLY round trip ---------------------------------------
    for (int big_endian = 0; big_endian <= 1; ++big_endian) {
        const std::string path = big_endian ? "confidence_sweep_selftest_be.ply"
                                            : "confidence_sweep_selftest_le.ply";
        std::string error;
        check(write_binary_ply(path, cloud, big_endian != 0), "the binary PLY is written");
        Cloud reread;
        check(read_ply(path, &reread, &error), "the binary PLY reads back");
        check(reread.size() == cloud.size(), "the binary PLY keeps every point");
        double worst = 0.0;
        for (std::size_t i = 0; i < cloud.size() && i < reread.size(); ++i) {
            for (int a = 0; a < 3; ++a) {
                worst = std::max(worst, std::fabs(static_cast<double>(cloud.point(i)[a]) -
                                                  static_cast<double>(reread.point(i)[a])));
            }
        }
        check(worst < 1e-6, "the binary PLY round trip keeps the positions");
        std::remove(path.c_str());
    }

    // ---- the recorded session --------------------------------------------
    {
        std::vector<std::vector<float>> frames;
        for (int f = 0; f < 3; ++f) {
            std::vector<float> pts;
            for (int i = 0; i < 100; ++i) {
                pts.push_back(static_cast<float>(f * 100 + i));
                pts.push_back(0.0f);
                pts.push_back(0.0f);
            }
            frames.push_back(pts);
        }
        const std::string path = "confidence_sweep_selftest.rec";
        std::string error;
        check(write_record(path, frames), "the recorded session is written");
        Cloud last;
        check(read_record(path, RecMode::kLastFrame, 0, &last, &error), "the recorded session reads back");
        check(last.size() == 100, "the last frame's points are read");
        check(last.size() == 100 && last.point(0)[0] == 200.0f, "the last frame is the last one");
        Cloud all;
        check(read_record(path, RecMode::kUnion, 0, &all, &error), "the union reads back");
        check(all.size() == 300, "the union carries every frame's points");
        Cloud first;
        check(read_record(path, RecMode::kFrame, 0, &first, &error), "one frame reads back");
        check(first.size() == 100 && first.point(0)[0] == 0.0f, "the first frame is the first one");
        std::remove(path.c_str());
    }

    // ---- the confidence field file ---------------------------------------
    {
        const std::string path = "confidence_sweep_selftest.conf";
        {
            std::ofstream out(path, std::ios::trunc);
            out << std::setprecision(std::numeric_limits<float>::max_digits10);
            for (std::size_t i = 0; i < confidence.size(); ++i) {
                out << i << "," << confidence[i] << "\n";
            }
        }
        std::vector<float> field;
        std::string error;
        check(read_confidence_field(path, confidence.size(), &field, &error), "the confidence field reads back");
        check(field.size() == confidence.size(), "the field has one value per point");
        bool same = field.size() == confidence.size();
        for (std::size_t i = 0; same && i < field.size(); ++i) same = field[i] == confidence[i];
        check(same, "the field's values survive the round trip");
        std::vector<SweepRow> field_rows;
        const SweepSummary field_summary = run_sweep(field, errors, sweep, &field_rows);
        check(field_summary.rows_beaten == summary.rows_beaten, "the field drives the same sweep as the estimator");
        std::remove(path.c_str());
    }

    // ---- the absolute sweep ----------------------------------------------
    // In the absolute mode the threshold decides how many points survive, so
    // the row's fraction has to be the fraction actually kept - and the row
    // that is effectively full retention has to fall outside the acceptance
    // window (there, both curves are the same set of points).
    {
        SweepOptions absolute;
        absolute.absolute = true;
        absolute.absolute_min = -0.003;
        absolute.absolute_max = -0.00005;
        absolute.steps = 8;
        std::vector<SweepRow> abs_rows;
        const SweepSummary abs_summary = run_sweep(confidence, errors, absolute, &abs_rows);
        check(!abs_rows.empty(), "the absolute sweep produced rows");
        bool fractions_match = true;
        for (const SweepRow& r : abs_rows) {
            const double actual = static_cast<double>(r.n_retained) / static_cast<double>(cloud.size());
            if (r.n_retained > 0 && std::fabs(r.retained_fraction - actual) > 1e-12) fractions_match = false;
        }
        check(fractions_match, "the absolute sweep reports the fraction it actually kept");
        bool has_full_row = false;
        std::size_t inside = 0;
        for (const SweepRow& r : abs_rows) {
            if (r.n_retained == cloud.size()) has_full_row = true;
            if (r.retained_fraction > 0.0 && r.retained_fraction < 1.0) ++inside;
        }
        check(has_full_row, "the absolute sweep has an effectively-full row");
        check(abs_summary.rows_evaluated == inside,
              "the acceptance window counts exactly the rows that kept some but not all points");
        check(abs_summary.rows_evaluated < abs_rows.size(),
              "the effectively-full row is outside the acceptance window");
    }

    // ---- the sweep's minimum fraction ------------------------------------
    {
        SweepOptions high;
        high.min_fraction = 0.5;
        std::vector<SweepRow> high_rows;
        const SweepSummary high_summary = run_sweep(confidence, errors, high, &high_rows);
        check(high_summary.rows_evaluated > 0, "the sweep with a raised minimum produced rows");
        check(!high_rows.empty() && std::fabs(high_rows.back().retained_fraction - 0.5) < 1e-9,
              "the sweep stops at the requested minimum fraction");
        check(!high_rows.empty() && high_rows.back().retained_fraction != rows.back().retained_fraction,
              "a different minimum fraction changes the sweep");
    }

    // ---- the fitted plane ------------------------------------------------
    // The truth's normal for a real capture: the wall is square to the
    // scanner, so the fitted plane's normal is the wall's normal in the
    // cloud's frame, and the tape is the anchor for its offset.
    {
        const PlaneFit fit = fit_plane(cloud);
        check(fit.points == cloud.size(), "the fit used every point");
        // The scene is a plane with a step in one quadrant, so the fit tilts
        // by a fraction of a degree - but it must be the scene's plane.
        check(std::fabs(fit.nz) > 0.999 && std::fabs(fit.nx) < 0.01 && std::fabs(fit.ny) < 0.01,
              "the fitted plane's normal is the scene's");
        check(std::fabs(fit.d) < 1e-3, "the fitted plane is at the scene's own offset");
        check(fit.rms > 0.0 && fit.rms < 0.01, "the fitted plane's residual is the scene's own roughness");
        // The fit's residual is the whole cloud's distance to its own best-fit
        // plane: no worse than the distance to the truth plane, and the same
        // order of magnitude (the fit absorbs the scene's step).
        check(fit.rms <= rows.front().rmse + 1e-12,
              "the fit's residual is no worse than the truth plane's");
        check(fit.rms > 0.5 * rows.front().rmse,
              "the fit's residual is the same order as the truth plane's");
    }

    if (g_failures > 0) {
        std::printf("[FAIL] %d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("[ ok ] the confidence/retention harness's self-test passed\n");
    return 0;
}
