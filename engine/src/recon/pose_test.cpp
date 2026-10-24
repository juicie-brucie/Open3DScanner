// WO-A1's device-free pose test: a known rigid transform applied to synthetic
// points is recovered from the resulting correspondences. The bounds below are
// measured, not assumed, and the estimator is exercised through the
// PoseEstimator seam so the strategy indirection is covered too.
#include "recon/pose.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace recon = openscanner::recon;

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++g_failures;
}

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

// A proper rotation from an axis-angle (Rodrigues), as a ground-truth Pose.
recon::Pose MakePose(double ax, double ay, double az, double angle, double tx, double ty, double tz) {
    const double n = std::sqrt(ax * ax + ay * ay + az * az);
    ax /= n; ay /= n; az /= n;
    const double c = std::cos(angle), s = std::sin(angle), C = 1 - c;
    recon::Pose p;
    p.r[0] = c + ax * ax * C;      p.r[1] = ax * ay * C - az * s; p.r[2] = ax * az * C + ay * s;
    p.r[3] = ay * ax * C + az * s; p.r[4] = c + ay * ay * C;      p.r[5] = ay * az * C - ax * s;
    p.r[6] = az * ax * C - ay * s; p.r[7] = az * ay * C + ax * s; p.r[8] = c + az * az * C;
    p.t[0] = tx; p.t[1] = ty; p.t[2] = tz;
    return p;
}

double MaxRotDiff(const recon::Pose& a, const recon::Pose& b) {
    double m = 0.0;
    for (int i = 0; i < 9; ++i) m = std::max(m, std::fabs(a.r[i] - b.r[i]));
    return m;
}
double MaxTransDiff(const recon::Pose& a, const recon::Pose& b) {
    return std::max({std::fabs(a.t[0] - b.t[0]), std::fabs(a.t[1] - b.t[1]), std::fabs(a.t[2] - b.t[2])});
}

// |R R^T - I|_max and det(R): a recovered pose must be a proper orthonormal
// rotation (no reflection, no scale).
double OrthoError(const recon::Pose& p) {
    double worst = 0.0;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            double s = 0.0;
            for (int k = 0; k < 3; ++k) s += p.r[i * 3 + k] * p.r[j * 3 + k];
            worst = std::max(worst, std::fabs(s - (i == j ? 1.0 : 0.0)));
        }
    return worst;
}
double Det(const recon::Pose& p) {
    return p.r[0] * (p.r[4] * p.r[8] - p.r[5] * p.r[7]) -
           p.r[1] * (p.r[3] * p.r[8] - p.r[5] * p.r[6]) +
           p.r[2] * (p.r[3] * p.r[7] - p.r[4] * p.r[6]);
}

std::vector<recon::Correspondence> MakePairs(const recon::Pose& truth,
                                             const std::vector<recon::Point3>& from, double noise,
                                             Lcg* rng) {
    std::vector<recon::Correspondence> pairs;
    for (const recon::Point3& f : from) {
        recon::Point3 to = recon::Apply(truth, f);
        if (noise > 0.0) {
            to.x += noise * rng->Signed();
            to.y += noise * rng->Signed();
            to.z += noise * rng->Signed();
        }
        pairs.push_back({f, to, 1.0});
    }
    return pairs;
}

}  // namespace

int main() {
    const recon::KabschEstimator kabsch;
    Lcg rng(0xA11CEu);

    // --- a known transform is recovered exactly (noise-free) --------------
    double worst_rot = 0.0, worst_trans = 0.0;
    {
        const recon::Pose truth = MakePose(0.3, -0.7, 0.5, 0.9, 12.0, -4.0, 30.0);
        std::vector<recon::Point3> from;
        for (int i = 0; i < 40; ++i) from.push_back({rng.Signed() * 50, rng.Signed() * 50, rng.Signed() * 50});
        const auto est = kabsch.Estimate(MakePairs(truth, from, 0.0, &rng));
        Check(est.ok, "noise-free: estimate ok");
        worst_rot = MaxRotDiff(est.pose, truth);
        worst_trans = MaxTransDiff(est.pose, truth);
        Check(worst_rot < 1e-9 && worst_trans < 1e-9, "noise-free: R and t recovered to ~machine precision");
        Check(est.rms_residual < 1e-9, "noise-free: residual ~0");
        Check(OrthoError(est.pose) < 1e-9 && std::fabs(Det(est.pose) - 1.0) < 1e-9,
              "noise-free: recovered R is a proper rotation");
    }

    // --- a planar correspondence set (stresses the reflection case) -------
    {
        const recon::Pose truth = MakePose(0.1, 0.2, 1.0, 1.7, -5.0, 8.0, 2.0);
        std::vector<recon::Point3> from;
        for (int i = 0; i < 12; ++i) from.push_back({rng.Signed() * 40, rng.Signed() * 40, 0.0});  // z=0 plane
        const auto est = kabsch.Estimate(MakePairs(truth, from, 0.0, &rng));
        Check(est.ok, "planar set: estimate ok");
        Check(std::fabs(Det(est.pose) - 1.0) < 1e-9 && OrthoError(est.pose) < 1e-9,
              "planar set: no reflection — det(R)=+1, orthonormal");
        Check(MaxRotDiff(est.pose, truth) < 1e-9, "planar set: rotation recovered");
    }

    // --- with noise, the fit is sensible and still a proper rotation ------
    {
        const recon::Pose truth = MakePose(-0.4, 0.5, 0.2, 0.6, 3.0, 3.0, 3.0);
        std::vector<recon::Point3> from;
        for (int i = 0; i < 200; ++i) from.push_back({rng.Signed() * 60, rng.Signed() * 60, rng.Signed() * 60});
        const double noise = 0.05;
        const auto est = kabsch.Estimate(MakePairs(truth, from, noise, &rng));
        Check(est.ok && est.used == 200, "noisy: estimate ok, all pairs used");
        // Translation stays within a small multiple of the per-point noise.
        Check(MaxTransDiff(est.pose, truth) < 0.05, "noisy: translation close to truth");
        Check(est.rms_residual > 0.0 && est.rms_residual < 3.0 * noise, "noisy: residual on the order of the noise");
        Check(OrthoError(est.pose) < 1e-9 && std::fabs(Det(est.pose) - 1.0) < 1e-9,
              "noisy: still a proper rotation");
        std::printf("[pose] noisy fit: rms_residual=%.4f (noise=%.3f)\n", est.rms_residual, noise);
    }

    // --- fewer than three pairs, and degenerate sets, are not ok ----------
    {
        std::vector<recon::Correspondence> two = {{{0, 0, 0}, {1, 0, 0}, 1.0}, {{1, 0, 0}, {2, 0, 0}, 1.0}};
        Check(!kabsch.Estimate(two).ok, "fewer than 3 pairs: not ok");

        std::vector<recon::Correspondence> collinear;
        for (int i = 0; i < 6; ++i) {
            recon::Point3 f{static_cast<double>(i), 0, 0};  // all on the x-axis
            collinear.push_back({f, {f.x + 5, f.y, f.z}, 1.0});
        }
        Check(!kabsch.Estimate(collinear).ok, "collinear points: degenerate, not ok");
    }

    // --- identity: from == to recovers the identity -----------------------
    {
        std::vector<recon::Correspondence> pairs;
        for (int i = 0; i < 5; ++i) {
            recon::Point3 f{rng.Signed() * 10, rng.Signed() * 10, rng.Signed() * 10};
            pairs.push_back({f, f, 1.0});
        }
        const auto est = kabsch.Estimate(pairs);
        Check(est.ok && MaxRotDiff(est.pose, recon::Pose{}) < 1e-9 && MaxTransDiff(est.pose, recon::Pose{}) < 1e-9,
              "identity: from==to gives the identity pose");
    }

    // --- Apply / Compose / Invert round-trips -----------------------------
    {
        const recon::Pose a = MakePose(0.2, 0.3, 0.9, 0.5, 1, 2, 3);
        const recon::Pose b = MakePose(0.9, -0.1, 0.2, -0.8, -4, 5, 6);
        const recon::Point3 x{7, -8, 9};
        const recon::Point3 via_compose = recon::Apply(recon::Compose(a, b), x);
        const recon::Point3 via_sequential = recon::Apply(a, recon::Apply(b, x));
        Check(std::fabs(via_compose.x - via_sequential.x) < 1e-9 &&
                  std::fabs(via_compose.y - via_sequential.y) < 1e-9 &&
                  std::fabs(via_compose.z - via_sequential.z) < 1e-9,
              "Compose(a,b) == a after b");
        const recon::Point3 back = recon::Apply(recon::Invert(a), recon::Apply(a, x));
        Check(std::fabs(back.x - x.x) < 1e-9 && std::fabs(back.y - x.y) < 1e-9 && std::fabs(back.z - x.z) < 1e-9,
              "Invert(a) undoes a");
    }

    // --- the estimator seam: drive it through the base interface ----------
    {
        const recon::PoseEstimator* est = &kabsch;
        Check(std::string(est->name()) == "kabsch-svd", "PoseEstimator seam: named strategy is callable via the interface");
    }

    std::printf("[pose] worst noise-free recovery: rot=%.3e trans=%.3e\n", worst_rot, worst_trans);
    std::printf("[pose] %d check(s), %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
