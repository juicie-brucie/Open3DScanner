// WO-A2's device-free fuse test: synthetic posed frames with a known answer.
// Covers incremental accumulation, voxel downsampling and determinism, ICP
// convergence from a perturbed pose, and the pairwise consistency metric with a
// drift-then-ICP-recovers scenario. No device, no recorded data.
#include "recon/fuse.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "recon/pose.h"

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
    double Signed() {
        state_ = state_ * 1664525u + 1013904223u;
        return (static_cast<double>(state_ >> 8) / 16777216.0) * 2.0 - 1.0;
    }

private:
    uint32_t state_;
};

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

std::vector<recon::Point3> RandomCloud(int n, double spread, Lcg* rng) {
    std::vector<recon::Point3> v;
    for (int i = 0; i < n; ++i) v.push_back({rng->Signed() * spread, rng->Signed() * spread, rng->Signed() * spread});
    return v;
}

double MaxRotDiff(const recon::Pose& a, const recon::Pose& b) {
    double m = 0.0;
    for (int i = 0; i < 9; ++i) m = std::max(m, std::fabs(a.r[i] - b.r[i]));
    return m;
}

}  // namespace

int main() {
    Lcg rng(0xF0C2u);

    // --- incremental: voxel-less keeps every posed point ------------------
    {
        recon::IncrementalFuser fuser(0.0);
        const auto cloud = RandomCloud(50, 20.0, &rng);
        const recon::Pose p = MakePose(0, 0, 1, 0.3, 5, 0, 0);
        fuser.Add(cloud, p);
        fuser.Add(cloud, recon::Pose{});  // identity
        Check(fuser.frame_count() == 2 && fuser.point_count() == 100, "incremental (no voxel): all posed points kept");
        const auto model = fuser.Model();
        // The identity-posed copy must appear verbatim in the model.
        bool found = false;
        for (const auto& m : model)
            if (std::fabs(m.x - cloud[0].x) < 1e-12 && std::fabs(m.y - cloud[0].y) < 1e-12 &&
                std::fabs(m.z - cloud[0].z) < 1e-12) { found = true; break; }
        Check(found, "incremental: an identity-posed point lands unchanged in the model");
    }

    // --- incremental: voxel centroid and bounded size --------------------
    {
        recon::IncrementalFuser fuser(1.0);  // 1-unit voxels
        // Three points inside the same voxel [0,1)^3 -> one model point at their centroid.
        fuser.Add({{0.1, 0.1, 0.1}, {0.2, 0.2, 0.2}, {0.3, 0.3, 0.3}}, recon::Pose{});
        const auto model = fuser.Model();
        Check(model.size() == 1 && fuser.point_count() == 1, "incremental (voxel): one occupied voxel");
        Check(std::fabs(model[0].x - 0.2) < 1e-9 && std::fabs(model[0].y - 0.2) < 1e-9,
              "incremental (voxel): the model point is the voxel centroid");
        // Re-adding the same frame many times does not grow the model.
        for (int i = 0; i < 20; ++i) fuser.Add({{0.1, 0.1, 0.1}}, recon::Pose{});
        Check(fuser.Model().size() == 1, "incremental (voxel): model size is bounded by the volume, not frames");
    }

    // --- batch fuse: union of posed frames, and deterministic ------------
    {
        const auto c0 = RandomCloud(30, 15.0, &rng);
        const auto c1 = RandomCloud(30, 15.0, &rng);
        const std::vector<recon::FrameCloud> frames = {{c0, MakePose(1, 0, 0, 0.2, 0, 0, 0)},
                                                       {c1, MakePose(0, 1, 0, -0.1, 3, 1, 0)}};
        const auto fused = recon::FuseBatch(frames, 0.0);
        Check(fused.size() == 60, "batch (no voxel): all posed points merged");
        const auto again = recon::FuseBatch(frames, 0.0);
        bool identical = fused.size() == again.size();
        for (size_t i = 0; identical && i < fused.size(); ++i)
            identical = (fused[i].x == again[i].x && fused[i].y == again[i].y && fused[i].z == again[i].z);
        Check(identical, "batch: output is deterministic");

        // Many points in a tight cluster collapse to few voxels.
        std::vector<recon::Point3> dense;
        for (int i = 0; i < 500; ++i) dense.push_back({rng.Signed() * 0.4, rng.Signed() * 0.4, rng.Signed() * 0.4});
        const auto reduced = recon::FuseBatch({{dense, recon::Pose{}}}, 1.0);
        Check(reduced.size() < dense.size() && !reduced.empty(), "batch (voxel): downsampling reduces the count");
    }

    // --- ICP converges from a perturbed pose -----------------------------
    {
        const auto target = RandomCloud(120, 30.0, &rng);
        const recon::Pose truth = MakePose(0.2, -0.4, 0.9, 0.12, 0.8, -0.5, 0.3);  // small motion
        // source mapped by truth lands exactly on target: Apply(truth, source_i) == target_i.
        const recon::Pose inv = recon::Invert(truth);
        std::vector<recon::Point3> source;
        for (const auto& t : target) source.push_back(recon::Apply(inv, t));

        const auto res = recon::IcpRefine(source, target, recon::Pose{} /* start at identity */);
        Check(res.ok, "icp: converged");
        Check(res.rms < 1e-6, "icp: final residual ~0");
        Check(MaxRotDiff(res.pose, truth) < 1e-6, "icp: recovered the true rotation");
        std::printf("[fuse] icp iterations=%d rms=%.3e\n", res.iterations, res.rms);

        Check(!recon::IcpRefine({{0, 0, 0}, {1, 0, 0}}, target, recon::Pose{}).ok, "icp: <3 source points -> not ok");
        Check(!recon::IcpRefine(source, {}, recon::Pose{}).ok, "icp: empty target -> not ok");
    }

    // --- consistency: correct poses agree; drift breaks it; ICP restores --
    {
        const auto M = RandomCloud(150, 25.0, &rng);  // the shared geometry, in the model frame
        // Two frames that both observe M; with correct poses they map to M.
        std::vector<recon::FrameCloud> frames = {{M, recon::Pose{}}, {M, recon::Pose{}}};
        const double tau = 1.0;
        Check(recon::PairwiseConsistencyRms(frames, tau) < 1e-9, "consistency: correct poses -> ~0");

        // Corrupt frame 1's pose with a small translation: it no longer agrees.
        const recon::Pose drift = MakePose(0, 0, 1, 0.0, 0.3, 0.0, 0.0);  // +0.3 in x
        frames[1].pose = drift;
        const double drifted = recon::PairwiseConsistencyRms(frames, tau);
        Check(std::fabs(drifted - 0.3) < 1e-6, "consistency: a 0.3 pose drift shows as ~0.3 inconsistency");

        // ICP frame 1 (source = M under its drifted pose's geometry) back onto
        // frame 0's model points; the recovered pose restores agreement.
        std::vector<recon::Point3> a0;
        for (const auto& p : M) a0.push_back(recon::Apply(frames[0].pose, p));
        const auto fixed = recon::IcpRefine(frames[1].points, a0, frames[1].pose);
        Check(fixed.ok, "consistency: ICP refine ok");
        frames[1].pose = fixed.pose;
        Check(recon::PairwiseConsistencyRms(frames, tau) < 1e-6, "consistency: ICP restores agreement");
    }

    std::printf("[fuse] %d check(s), %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
