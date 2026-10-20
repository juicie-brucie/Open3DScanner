// confidence_sweep - the confidence/retention experiment's harness (row #159).
//
// Scores a cloud against a ground truth, estimates a per-point confidence,
// sweeps a retention threshold, and reports the accuracy of what was kept
// next to the accuracy of an equal-sized RANDOM keep. The random keep is the
// point of the tool: a confidence signal only counts if it beats random
// retention at the same retained fraction.
//
// The estimator, its assumptions and the conditions that would falsify it are
// written up in
// docs/recomp/log/2026-10-20-row-159-confidence-experiment.md.
//
// Exit codes: 0 = the run completed, 1 = a runtime failure, 2 = bad
// arguments, 3 = --expect-beats-random and the confidence curve did not beat
// random retention, 4 = the fitted plane's offset did not agree with the
// taped distance.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "harness.h"

namespace {

using openscanner::confidence::Cloud;
using openscanner::confidence::CloudSource;
using openscanner::confidence::RecMode;
using openscanner::confidence::ResidualOptions;
using openscanner::confidence::SweepOptions;
using openscanner::confidence::SweepRow;
using openscanner::confidence::SweepSummary;
using openscanner::confidence::SyntheticSpec;
using openscanner::confidence::Truth;

void usage() {
    std::fprintf(stderr,
                 "confidence_sweep - accuracy against retained points, with a random control\n"
                 "\n"
                 "Input (exactly one):\n"
                 "  --synthetic               a generated plane with a biased, noisy region\n"
                 "  --ply FILE                a PLY cloud (ascii or binary)\n"
                 "  --rec FILE                a recorded V1 session; --rec-mode picks the cloud\n"
                 "\n"
                 "Ground truth:\n"
                 "  --truth plane NX NY NZ D  a plane with the given normal and offset\n"
                 "  --truth sphere CX CY CZ R a sphere with the given centre and radius\n"
                 "  --truth ply FILE          a point set; a point's distance is to the nearest\n"
                 "  --fit-plane               fit a plane to the cloud and report it (the truth's\n"
                 "                            normal, its offset and the fit's residual)\n"
                 "  --fit-plane-check D       check the fitted offset against the taped distance D\n"
                 "  --fit-plane-tolerance T   the tolerance for that check, in metres (default 0.002)\n"
                 "\n"
                 "Confidence (higher = more trusted):\n"
                 "  --confidence residual     the per-point local planarity residual (default)\n"
                 "  --confidence file FILE    a per-point field: 'index,value' or 'value' per line\n"
                 "  --k N                     neighbours in the local fit (default 16)\n"
                 "  --max-radius F            the search radius, in median spacings (default 24)\n"
                 "\n"
                 "Sweep:\n"
                 "  --sweep-steps N           rows in the sweep (default 21)\n"
                 "  --sweep-min-fraction F    the smallest retained fraction the sweep reaches\n"
                 "                            (default 0.05)\n"
                 "  --absolute                sweep a raw confidence value instead of a quantile\n"
                 "  --sweep-min X --sweep-max Y   the range for --absolute\n"
                 "  --random-trials N         random keeps averaged per row (default 20)\n"
                 "  --seed N                  the control's seed (default 20261020)\n"
                 "\n"
                 "Output:\n"
                 "  --csv FILE                the sweep table\n"
                 "  --plot FILE               the sweep's SVG\n"
                 "  --expect-beats-random     exit 3 unless the confidence curve beats random\n"
                 "  --min-improvement F       the improvement the acceptance also requires\n"
                 "  --stride N                keep every Nth point\n"
                 "  --max-points N            keep at most N points\n"
                 "  --quiet                   no summary on stdout\n");
}

bool parse_double(const char* text, double* out) {
    char* end = nullptr;
    const double v = std::strtod(text, &end);
    if (end == text || *end != '\0') return false;
    *out = v;
    return true;
}

bool parse_int(const char* text, long* out) {
    char* end = nullptr;
    const long v = std::strtol(text, &end, 10);
    if (end == text || *end != '\0') return false;
    *out = v;
    return true;
}

std::string source_name(CloudSource source) {
    switch (source) {
        case CloudSource::kPly: return "ply";
        case CloudSource::kRec: return "recorded session";
        case CloudSource::kSynthetic: return "synthetic scene";
    }
    return "unknown";
}

bool write_csv(const std::string& path, const std::vector<SweepRow>& rows, const std::string& title,
               std::string* error) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        if (error) *error = "cannot write " + path;
        return false;
    }
    out << "# confidence_sweep sweep\n";
    out << "# " << title << "\n";
    out << "# the accuracy columns are metres; the random columns are the control's mean over the "
           "trials\n";
    out << "# the threshold is on the confidence scale (higher = more trusted); for the residual "
           "estimator the confidence is the negated residual\n";
    out << "threshold,retained_fraction,n_retained,rmse,p95,median,rmse_random_mean,rmse_random_std,"
           "p95_random_mean,median_random_mean\n";
    char buf[512];
    for (const SweepRow& r : rows) {
        std::snprintf(buf, sizeof(buf), "%.10g,%.10g,%zu,%.10g,%.10g,%.10g,%.10g,%.10g,%.10g,%.10g\n",
                      r.threshold, r.retained_fraction, r.n_retained, r.rmse, r.p95, r.median,
                      r.rmse_random_mean, r.rmse_random_std, r.p95_random_mean, r.median_random_mean);
        out << buf;
    }
    if (!out) {
        if (error) *error = "the write of " + path + " failed";
        return false;
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    std::string ply_path;
    std::string rec_path;
    bool use_synthetic = false;
    std::string truth_kind = "plane";
    double truth_values[4] = {0, 0, 1, 0};
    std::string truth_ply;
    std::string confidence_kind = "residual";
    std::string confidence_path;
    ResidualOptions residual_options;
    SweepOptions sweep;
    std::string csv_path;
    std::string plot_path;
    bool expect_beats_random = false;
    double min_improvement = 0.0;
    long stride = 1;
    long max_points = 0;
    bool quiet = false;
    RecMode rec_mode = RecMode::kLastFrame;
    int rec_frame = 0;
    bool fit_plane = false;
    bool fit_plane_check = false;
    double fit_plane_expected = 0.0;
    double fit_plane_tolerance = 0.002;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto need = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "confidence_sweep: %s needs a value\n", name);
                std::exit(2);
            }
            return argv[++i];
        };
        if (arg == "--help" || arg == "-h") {
            usage();
            return 0;
        } else if (arg == "--synthetic") {
            use_synthetic = true;
        } else if (arg == "--ply") {
            ply_path = need("--ply");
        } else if (arg == "--rec") {
            rec_path = need("--rec");
        } else if (arg == "--rec-mode") {
            const std::string mode = need("--rec-mode");
            if (mode == "last") {
                rec_mode = RecMode::kLastFrame;
            } else if (mode == "union") {
                rec_mode = RecMode::kUnion;
            } else if (mode == "frame") {
                rec_mode = RecMode::kFrame;
            } else {
                std::fprintf(stderr, "confidence_sweep: --rec-mode takes last, union or frame\n");
                return 2;
            }
        } else if (arg == "--rec-frame") {
            long v = 0;
            if (!parse_int(need("--rec-frame"), &v) || v < 0) {
                std::fprintf(stderr, "confidence_sweep: --rec-frame needs a non-negative integer\n");
                return 2;
            }
            rec_frame = static_cast<int>(v);
            rec_mode = RecMode::kFrame;
        } else if (arg == "--truth") {
            const std::string kind = need("--truth");
            if (kind == "plane" || kind == "sphere") {
                truth_kind = kind;
                for (int k = 0; k < 4; ++k) {
                    if (!parse_double(need("--truth"), &truth_values[k])) {
                        std::fprintf(stderr, "confidence_sweep: --truth %s needs four numbers\n", kind.c_str());
                        return 2;
                    }
                }
            } else if (kind == "ply") {
                truth_kind = "ply";
                truth_ply = need("--truth");
            } else {
                std::fprintf(stderr, "confidence_sweep: --truth takes plane, sphere or ply\n");
                return 2;
            }
        } else if (arg == "--confidence") {
            const std::string kind = need("--confidence");
            if (kind == "residual") {
                confidence_kind = "residual";
            } else if (kind == "file") {
                confidence_kind = "file";
                confidence_path = need("--confidence");
            } else {
                std::fprintf(stderr, "confidence_sweep: --confidence takes residual or file\n");
                return 2;
            }
        } else if (arg == "--k") {
            long v = 0;
            if (!parse_int(need("--k"), &v) || v < 3) {
                std::fprintf(stderr, "confidence_sweep: --k needs an integer of at least 3\n");
                return 2;
            }
            residual_options.k = static_cast<int>(v);
        } else if (arg == "--max-radius") {
            if (!parse_double(need("--max-radius"), &residual_options.max_radius_factor) || residual_options.max_radius_factor <= 0.0) {
                std::fprintf(stderr, "confidence_sweep: --max-radius needs a positive number\n");
                return 2;
            }
        } else if (arg == "--sweep-steps") {
            long v = 0;
            if (!parse_int(need("--sweep-steps"), &v) || v < 2) {
                std::fprintf(stderr, "confidence_sweep: --sweep-steps needs an integer of at least 2\n");
                return 2;
            }
            sweep.steps = static_cast<int>(v);
        } else if (arg == "--sweep-min-fraction") {
            if (!parse_double(need("--sweep-min-fraction"), &sweep.min_fraction) || sweep.min_fraction < 0.0 ||
                sweep.min_fraction >= 1.0) {
                std::fprintf(stderr, "confidence_sweep: --sweep-min-fraction needs a number in [0, 1)\n");
                return 2;
            }
        } else if (arg == "--fit-plane") {
            fit_plane = true;
        } else if (arg == "--fit-plane-check") {
            if (!parse_double(need("--fit-plane-check"), &fit_plane_expected)) {
                std::fprintf(stderr, "confidence_sweep: --fit-plane-check needs a number\n");
                return 2;
            }
            fit_plane = true;
            fit_plane_check = true;
        } else if (arg == "--fit-plane-tolerance") {
            if (!parse_double(need("--fit-plane-tolerance"), &fit_plane_tolerance) || fit_plane_tolerance < 0.0) {
                std::fprintf(stderr, "confidence_sweep: --fit-plane-tolerance needs a number of at least 0\n");
                return 2;
            }
        } else if (arg == "--absolute") {
            sweep.absolute = true;
        } else if (arg == "--sweep-min") {
            if (!parse_double(need("--sweep-min"), &sweep.absolute_min)) return 2;
        } else if (arg == "--sweep-max") {
            if (!parse_double(need("--sweep-max"), &sweep.absolute_max)) return 2;
        } else if (arg == "--random-trials") {
            long v = 0;
            if (!parse_int(need("--random-trials"), &v) || v < 1) {
                std::fprintf(stderr, "confidence_sweep: --random-trials needs an integer of at least 1\n");
                return 2;
            }
            sweep.random_trials = static_cast<int>(v);
        } else if (arg == "--seed") {
            long v = 0;
            if (!parse_int(need("--seed"), &v)) {
                std::fprintf(stderr, "confidence_sweep: --seed needs an integer\n");
                return 2;
            }
            sweep.seed = static_cast<uint64_t>(v);
        } else if (arg == "--csv") {
            csv_path = need("--csv");
        } else if (arg == "--plot") {
            plot_path = need("--plot");
        } else if (arg == "--expect-beats-random") {
            expect_beats_random = true;
        } else if (arg == "--min-improvement") {
            if (!parse_double(need("--min-improvement"), &min_improvement)) return 2;
        } else if (arg == "--stride") {
            if (!parse_int(need("--stride"), &stride) || stride < 1) {
                std::fprintf(stderr, "confidence_sweep: --stride needs an integer of at least 1\n");
                return 2;
            }
        } else if (arg == "--max-points") {
            if (!parse_int(need("--max-points"), &max_points) || max_points < 1) {
                std::fprintf(stderr, "confidence_sweep: --max-points needs an integer of at least 1\n");
                return 2;
            }
        } else if (arg == "--quiet") {
            quiet = true;
        } else {
            std::fprintf(stderr, "confidence_sweep: unknown argument %s\n", arg.c_str());
            usage();
            return 2;
        }
    }

    const int sources = (use_synthetic ? 1 : 0) + (ply_path.empty() ? 0 : 1) + (rec_path.empty() ? 0 : 1);
    if (sources != 1) {
        std::fprintf(stderr, "confidence_sweep: pick exactly one of --synthetic, --ply or --rec\n");
        return 2;
    }

    // ---- the cloud -------------------------------------------------------
    Cloud cloud;
    CloudSource source = CloudSource::kSynthetic;
    std::string source_detail;
    std::string error;
    if (use_synthetic) {
        SyntheticSpec spec;
        source = CloudSource::kSynthetic;
        source_detail = "the synthetic plane scene";
        cloud = openscanner::confidence::make_synthetic_scene(spec, nullptr);
    } else if (!ply_path.empty()) {
        source = CloudSource::kPly;
        source_detail = ply_path;
        if (!openscanner::confidence::read_ply(ply_path, &cloud, &error)) {
            std::fprintf(stderr, "confidence_sweep: %s\n", error.c_str());
            return 1;
        }
    } else {
        source = CloudSource::kRec;
        source_detail = rec_path;
        if (!openscanner::confidence::read_record(rec_path, rec_mode, rec_frame, &cloud, &error)) {
            std::fprintf(stderr, "confidence_sweep: %s\n", error.c_str());
            return 1;
        }
    }
    if (stride > 1 || max_points > 0) {
        Cloud reduced;
        for (std::size_t i = 0; i < cloud.size(); i += static_cast<std::size_t>(stride)) {
            if (max_points > 0 && static_cast<long>(reduced.size()) >= max_points) break;
            const float* p = cloud.point(i);
            reduced.xyz.push_back(p[0]);
            reduced.xyz.push_back(p[1]);
            reduced.xyz.push_back(p[2]);
        }
        if (reduced.size() < cloud.size()) {
            source_detail += " (subsampled to " + std::to_string(reduced.size()) + " points)";
        }
        cloud = std::move(reduced);
    }
    if (cloud.size() < 8) {
        std::fprintf(stderr, "confidence_sweep: the cloud needs at least eight points\n");
        return 1;
    }

    // ---- the ground truth ------------------------------------------------
    Truth truth;
    if (truth_kind == "plane") {
        truth = Truth::plane(truth_values[0], truth_values[1], truth_values[2], truth_values[3]);
    } else if (truth_kind == "sphere") {
        truth = Truth::sphere(truth_values[0], truth_values[1], truth_values[2], truth_values[3]);
    } else {
        Cloud truth_cloud;
        if (!openscanner::confidence::read_ply(truth_ply, &truth_cloud, &error)) {
            std::fprintf(stderr, "confidence_sweep: %s\n", error.c_str());
            return 1;
        }
        truth = Truth::point_set(truth_cloud.xyz);
    }
    if (use_synthetic && truth_kind == "plane" && truth_values[0] == 0.0 && truth_values[1] == 0.0 &&
        truth_values[2] == 1.0 && truth_values[3] == 0.0) {
        truth = openscanner::confidence::synthetic_truth();
    }

    // ---- the fitted plane ------------------------------------------------
    // The truth's normal for a real capture: the wall is square to the
    // scanner, so its normal in the cloud's frame is the fitted plane's, and
    // the tape is the anchor for its offset. See the design note's §6.2.
    if (fit_plane) {
        const openscanner::confidence::PlaneFit fit = openscanner::confidence::fit_plane(cloud);
        char buf[512];
        std::snprintf(buf, sizeof(buf),
                      "[fit] normal (%.6g, %.6g, %.6g), fitted offset %.6g m, fit residual: rms %.6g m, "
                      "p95 %.6g m, over %zu points\n",
                      fit.nx, fit.ny, fit.nz, fit.d, fit.rms, fit.p95, fit.points);
        std::fputs(buf, stdout);
        std::printf("[fit] the truth's plane: --truth plane %.6g %.6g %.6g <the taped distance>\n", fit.nx,
                    fit.ny, fit.nz);
        if (fit_plane_check) {
            const double delta = fit.d - fit_plane_expected;
            std::printf("[fit] the fitted offset %.6g m against the taped %.6g m: delta %+.6g m "
                        "(tolerance %.6g m)\n",
                        fit.d, fit_plane_expected, delta, fit_plane_tolerance);
            if (std::fabs(delta) > fit_plane_tolerance) {
                std::fprintf(stderr,
                             "confidence_sweep: FAIL - the fitted offset is %+.6g m from the taped distance, "
                             "beyond the %.6g m tolerance. The fit did not find the wall (or the scene is "
                             "wrong); do not trust the sweep's numbers.\n",
                             delta, fit_plane_tolerance);
                return 4;
            }
            std::printf("[fit] OK - the fit agrees with the tape.\n");
        }
    }

    // ---- the errors and the confidence ------------------------------------
    std::vector<double> errors(cloud.size());
    for (std::size_t i = 0; i < cloud.size(); ++i) {
        const float* p = cloud.point(i);
        errors[i] = truth.distance(p[0], p[1], p[2]);
    }
    std::vector<float> confidence;
    std::string confidence_detail;
    if (confidence_kind == "file") {
        if (!openscanner::confidence::read_confidence_field(confidence_path, cloud.size(), &confidence, &error)) {
            std::fprintf(stderr, "confidence_sweep: %s\n", error.c_str());
            return 1;
        }
        confidence_detail = "the field in " + confidence_path;
    } else {
        const std::vector<float> residual_values =
            openscanner::confidence::local_residual(cloud, residual_options, &error);
        if (residual_values.empty()) {
            std::fprintf(stderr, "confidence_sweep: %s\n", error.empty() ? "the residual failed" : error.c_str());
            return 1;
        }
        // The residual is a cost: the lower it is, the more trusted the point.
        confidence.resize(residual_values.size());
        for (std::size_t i = 0; i < residual_values.size(); ++i) confidence[i] = -residual_values[i];
        confidence_detail = "the local planarity residual (k=" + std::to_string(residual_options.k) + ")";
    }

    // ---- the sweep --------------------------------------------------------
    const std::string title = source_detail + " - " + truth.describe();
    std::vector<SweepRow> rows;
    const SweepSummary summary = openscanner::confidence::run_sweep(confidence, errors, sweep, &rows);

    if (!csv_path.empty() && !write_csv(csv_path, rows, title, &error)) {
        std::fprintf(stderr, "confidence_sweep: %s\n", error.c_str());
        return 1;
    }
    if (!plot_path.empty()) {
        const std::string subtitle = title + " - " + confidence_detail;
        if (!openscanner::confidence::write_svg(plot_path, rows, "Confidence retention vs random retention",
                                                subtitle, &error)) {
            std::fprintf(stderr, "confidence_sweep: %s\n", error.c_str());
            return 1;
        }
    }

    if (!quiet) {
        std::printf("confidence_sweep: %zu points from %s (%s)\n", cloud.size(), source_detail.c_str(),
                    source_name(source).c_str());
        std::printf("confidence_sweep: truth %s\n", truth.describe().c_str());
        std::printf("confidence_sweep: confidence %s\n", confidence_detail.c_str());
        std::printf("%-10s %-10s %-12s %-12s %-12s %-12s\n", "retained", "threshold", "rmse", "p95", "rmse_random",
                    "p95_random");
        char buf[256];
        for (const SweepRow& r : rows) {
            std::snprintf(buf, sizeof(buf), "%-10.3f %-10.4g %-12.6g %-12.6g %-12.6g %-12.6g", r.retained_fraction,
                          r.threshold, r.rmse, r.p95, r.rmse_random_mean, r.p95_random_mean);
            std::printf("%s\n", buf);
        }
        std::printf("[summary] the acceptance window: %zu of %zu sweep rows\n", summary.rows_evaluated,
                    rows.size());
        std::printf("[summary] confidence beats random retention at %zu of %zu rows (%.1f%%)\n",
                    summary.rows_beaten, summary.rows_evaluated, 100.0 * summary.beats_random_fraction);
        std::printf("[summary] mean relative improvement over random: %.1f%% (worst row %.1f%%)\n",
                    100.0 * summary.mean_relative_improvement, 100.0 * summary.min_relative_improvement);
        std::printf("[summary] area between the curves: %.6g m\n", summary.area_gap);
        if (!csv_path.empty()) std::printf("[summary] csv %s\n", csv_path.c_str());
        if (!plot_path.empty()) std::printf("[summary] plot %s\n", plot_path.c_str());
    }

    if (expect_beats_random) {
        const bool ok = openscanner::confidence::meets_acceptance(summary, min_improvement);
        if (!ok) {
            std::fprintf(stderr,
                         "confidence_sweep: FAIL - the confidence curve did not beat random retention "
                         "(%.1f%% of the rows, mean improvement %.1f%%, required %.1f%%)\n",
                         100.0 * summary.beats_random_fraction, 100.0 * summary.mean_relative_improvement,
                         100.0 * min_improvement);
            return 3;
        }
        if (!quiet) {
            std::printf("[summary] PASS - the confidence curve beats random retention\n");
        }
    }
    return 0;
}
