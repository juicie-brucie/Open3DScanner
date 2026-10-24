// WO-2's device-free A/B harness test: the cloud-compare metrics on synthetic
// clouds whose answer is known by construction, plus a cross-check of the
// voxel-grid nearest-neighbour search against a brute-force search so the
// acceleration is proven exact.
#include "recon/cloud_compare.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
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

// A deterministic 0..1 generator (no <random>, for cross-platform stability).
class Lcg {
public:
    explicit Lcg(uint32_t seed) : state_(seed) {}
    double Next() {
        state_ = state_ * 1664525u + 1013904223u;
        return static_cast<double>(state_ >> 8) / 16777216.0;
    }

private:
    uint32_t state_;
};

double BruteNearest(const recon::Point3& p, const std::vector<recon::Point3>& b) {
    double best = std::numeric_limits<double>::infinity();
    for (const recon::Point3& q : b) {
        const double dx = p.x - q.x, dy = p.y - q.y, dz = p.z - q.z;
        best = std::min(best, dx * dx + dy * dy + dz * dz);
    }
    return std::sqrt(best);
}

}  // namespace

int main() {
    // --- identical clouds: zero distance, full coverage -------------------
    {
        std::vector<recon::Point3> a;
        Lcg rng(0xC1a0u);
        for (int i = 0; i < 300; ++i) a.push_back({rng.Next() * 100, rng.Next() * 100, rng.Next() * 100});
        const auto m = recon::CompareClouds(a, a, /*tau=*/0.1);
        Check(m.valid, "identical clouds: valid");
        Check(m.rms_nn < 1e-9 && m.mean_nn < 1e-9 && m.max_nn < 1e-9, "identical clouds: zero NN distance");
        Check(std::fabs(m.coverage - 1.0) < 1e-9, "identical clouds: coverage is 1");
        Check(m.count_a == 300 && m.count_b == 300, "identical clouds: counts reported");
    }

    // --- a known rigid shift ----------------------------------------------
    {
        // A regular grid 10 units apart; a copy shifted 0.5 in x. Each shifted
        // point's nearest original is the one it came from (0.5 away).
        std::vector<recon::Point3> base;
        for (int x = 0; x < 6; ++x)
            for (int y = 0; y < 6; ++y)
                for (int z = 0; z < 6; ++z) base.push_back({x * 10.0, y * 10.0, z * 10.0});
        std::vector<recon::Point3> shifted;
        for (const auto& p : base) shifted.push_back({p.x + 0.5, p.y, p.z});

        const auto near = recon::CompareClouds(shifted, base, /*tau=*/1.0);
        Check(std::fabs(near.rms_nn - 0.5) < 1e-9 && std::fabs(near.mean_nn - 0.5) < 1e-9,
              "shift 0.5: NN distance is the shift");
        Check(std::fabs(near.coverage - 1.0) < 1e-9, "shift 0.5 < tau 1.0: full coverage");

        const auto tight = recon::CompareClouds(shifted, base, /*tau=*/0.25);
        Check(tight.coverage < 1e-9, "shift 0.5 > tau 0.25: no coverage");
    }

    // --- A is a subset of B -----------------------------------------------
    {
        std::vector<recon::Point3> b;
        for (int i = 0; i < 100; ++i) b.push_back({i * 10.0, 0.0, 0.0});
        std::vector<recon::Point3> a(b.begin(), b.begin() + 50);  // first half
        const auto m = recon::CompareClouds(a, b, /*tau=*/0.25);
        Check(m.rms_nn < 1e-9, "subset: every A point is in B, so NN is 0");
        Check(std::fabs(m.coverage - 0.5) < 1e-9, "subset: coverage is the covered fraction of B");
    }

    // --- empty clouds and a non-positive tau are invalid ------------------
    {
        std::vector<recon::Point3> some{{0, 0, 0}};
        Check(!recon::CompareClouds({}, some, 1.0).valid, "empty A is invalid");
        Check(!recon::CompareClouds(some, {}, 1.0).valid, "empty B is invalid");
        Check(!recon::CompareClouds(some, some, 0.0).valid, "tau <= 0 is invalid");
    }

    // --- the voxel grid agrees with brute force (exactness of the search) --
    {
        Lcg rng(0x5EED42u);
        std::vector<recon::Point3> a, b;
        for (int i = 0; i < 200; ++i) a.push_back({rng.Next() * 50, rng.Next() * 50, rng.Next() * 50});
        for (int i = 0; i < 2000; ++i) b.push_back({rng.Next() * 50, rng.Next() * 50, rng.Next() * 50});
        // tau far smaller than the cloud extent, to force the grid's cell cap.
        const auto m = recon::CompareClouds(a, b, /*tau=*/0.05);

        double sum_sq = 0.0;
        for (const auto& p : a) {
            const double nn = BruteNearest(p, b);
            sum_sq += nn * nn;
        }
        const double brute_rms = std::sqrt(sum_sq / static_cast<double>(a.size()));
        Check(std::fabs(m.rms_nn - brute_rms) < 1e-9,
              "voxel-grid RMS-NN equals brute-force RMS-NN");
    }

    // --- Positions() pulls positions out of a triangulated cloud ----------
    {
        std::vector<recon::CloudPoint> cloud(2);
        cloud[0].position = {1, 2, 3};
        cloud[1].position = {4, 5, 6};
        const auto pts = recon::Positions(cloud);
        Check(pts.size() == 2 && pts[1].y == 5.0, "Positions() extracts the point positions");
    }

    std::printf("[cloud-compare] %d check(s), %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
