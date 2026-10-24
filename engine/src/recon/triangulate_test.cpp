// WO-2's device-free triangulation test: synthetic geometry with a known 3-D
// answer. A point on a known laser plane is projected through a pinhole camera
// (the forward model) into a sub-pixel laser-line centre; triangulation must
// invert that back to the original 3-D point. The bound reported below is
// measured, not assumed.
#include "recon/triangulate.h"

#include <cmath>
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

// The forward pinhole projection of a 3-D point to a pixel — independent of the
// triangulation under test.
void Project(const recon::CameraIntrinsics& k, const recon::Point3& p, double* u, double* v) {
    *u = k.fx * p.x / p.z + k.cx;
    *v = k.fy * p.y / p.z + k.cy;
}

// The plane a*X+b*Y+c*Z+d=0 through `on_plane` with normal (a,b,c).
recon::LaserPlane PlaneThrough(double a, double b, double c, const recon::Point3& on_plane) {
    return recon::LaserPlane{a, b, c, -(a * on_plane.x + b * on_plane.y + c * on_plane.z)};
}

double Dist(const recon::Point3& a, const recon::Point3& b) {
    const double dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// A one-centre frame at pixel (u, v_row), associated to `plane_id`.
recon::Result OneCentreFrame(double u, int v_row, std::vector<std::vector<int>>* assoc, int plane_id) {
    recon::Result frame;
    frame.valid = true;
    frame.width = 1920;
    frame.height = 1200;
    frame.rows.resize(static_cast<size_t>(v_row) + 1);
    recon::LineCenter centre;
    centre.y = v_row;
    centre.x = static_cast<float>(u);
    centre.confidence = 1.0f;
    frame.rows[v_row].status = recon::RowStatus::kFound;
    frame.rows[v_row].centres.push_back(centre);
    frame.centre_count = 1;
    assoc->assign(static_cast<size_t>(v_row) + 1, {});
    (*assoc)[v_row] = {plane_id};
    return frame;
}

}  // namespace

int main() {
    const recon::CameraIntrinsics k{1000.0, 1000.0, 960.0, 600.0};
    double worst_error = 0.0;

    // --- fronto-parallel plane Z=500, a sub-pixel column -------------------
    {
        const recon::Point3 truth{12.3, 74.0, 500.0};  // v = 1000*74/500+600 = 748 (integer row)
        double u, v;
        Project(k, truth, &u, &v);
        std::vector<std::vector<int>> assoc;
        recon::Result frame = OneCentreFrame(u, static_cast<int>(std::lround(v)), &assoc, 0);
        recon::Calibration cal{k, {PlaneThrough(0, 0, 1, truth)}};
        const auto cloud = recon::Triangulate(frame, cal, assoc);
        Check(cloud.size() == 1, "fronto-parallel: one point triangulated");
        const double e = cloud.empty() ? 1e9 : Dist(cloud[0].position, truth);
        worst_error = std::max(worst_error, e);
        Check(e < 1e-4, "fronto-parallel: recovered 3-D matches the known point");
    }

    // --- a tilted plane, varying depth -------------------------------------
    {
        const recon::Point3 truth{10.37, 20.0, 400.0};  // v = 1000*20/400+600 = 650
        double u, v;
        Project(k, truth, &u, &v);
        std::vector<std::vector<int>> assoc;
        recon::Result frame = OneCentreFrame(u, static_cast<int>(std::lround(v)), &assoc, 0);
        recon::Calibration cal{k, {PlaneThrough(0.2, 0.1, 1.0, truth)}};
        const auto cloud = recon::Triangulate(frame, cal, assoc);
        Check(cloud.size() == 1, "tilted plane: one point triangulated");
        const double e = cloud.empty() ? 1e9 : Dist(cloud[0].position, truth);
        worst_error = std::max(worst_error, e);
        Check(e < 1e-4, "tilted plane: recovered 3-D matches the known point");
    }

    // --- multi-plane: two centres, each on its own plane -------------------
    {
        const recon::Point3 p0{5.0, 30.0, 300.0};   // v = 1000*30/300+600 = 700
        const recon::Point3 p1{-8.0, 90.0, 600.0};  // v = 1000*90/600+600 = 750
        double u0, v0, u1, v1;
        Project(k, p0, &u0, &v0);
        Project(k, p1, &u1, &v1);
        recon::Result frame;
        frame.valid = true;
        frame.rows.resize(760);
        auto put = [&](double u, double v, int id, const recon::Point3&) {
            recon::LineCenter c;
            c.y = static_cast<int>(std::lround(v));
            c.x = static_cast<float>(u);
            frame.rows[c.y].centres.push_back(c);
        };
        std::vector<std::vector<int>> assoc(760);
        put(u0, v0, 0, p0);
        assoc[static_cast<int>(std::lround(v0))] = {0};
        put(u1, v1, 1, p1);
        assoc[static_cast<int>(std::lround(v1))] = {1};
        frame.centre_count = 2;
        recon::Calibration cal{k, {PlaneThrough(0, 0, 1, p0), PlaneThrough(0.1, -0.2, 1.0, p1)}};
        const auto cloud = recon::Triangulate(frame, cal, assoc);
        Check(cloud.size() == 2, "multi-plane: both centres triangulated to their own plane");
        if (cloud.size() == 2) {
            // Rows are visited top-down, so cloud[0] is p0's row (700), cloud[1] p1's (750).
            const double e0 = Dist(cloud[0].position, p0);
            const double e1 = Dist(cloud[1].position, p1);
            worst_error = std::max({worst_error, e0, e1});
            Check(e0 < 1e-4 && e1 < 1e-4, "multi-plane: each point lands on its associated plane");
        }
    }

    // --- a ray parallel to the plane is dropped ----------------------------
    {
        // Plane X = 100 (normal (1,0,0)); a pixel on the principal axis has a
        // ray (0,0,1), parallel to the plane -> no intersection.
        std::vector<std::vector<int>> assoc;
        recon::Result frame = OneCentreFrame(k.cx, static_cast<int>(k.cy), &assoc, 0);
        recon::Calibration cal{k, {recon::LaserPlane{1, 0, 0, -100}}};
        const auto cloud = recon::Triangulate(frame, cal, assoc);
        Check(cloud.empty(), "ray parallel to the plane is dropped");
    }

    // --- an intersection behind the camera is dropped ----------------------
    {
        // Plane Z = -500 is behind the camera; t would be negative.
        std::vector<std::vector<int>> assoc;
        recon::Result frame = OneCentreFrame(k.cx + 20.0, static_cast<int>(k.cy) + 10, &assoc, 0);
        recon::Calibration cal{k, {recon::LaserPlane{0, 0, 1, 500}}};  // Z = -500
        const auto cloud = recon::Triangulate(frame, cal, assoc);
        Check(cloud.empty(), "an intersection behind the camera is dropped");
    }

    // --- invalid intrinsics give an empty cloud ----------------------------
    {
        std::vector<std::vector<int>> assoc;
        recon::Result frame = OneCentreFrame(1000.0, 600, &assoc, 0);
        recon::Calibration cal{recon::CameraIntrinsics{0, 0, 960, 600}, {recon::LaserPlane{0, 0, 1, -500}}};
        const auto cloud = recon::Triangulate(frame, cal, assoc);
        Check(cloud.empty(), "invalid intrinsics give an empty cloud");
    }

    // --- an out-of-range or negative association drops the centre ----------
    {
        std::vector<std::vector<int>> assoc;
        recon::Result frame = OneCentreFrame(1000.0, 600, &assoc, 5);  // plane 5, only 1 plane
        recon::Calibration cal{k, {recon::LaserPlane{0, 0, 1, -500}}};
        const auto cloud = recon::Triangulate(frame, cal, assoc);
        Check(cloud.empty(), "an out-of-range plane id drops the centre");

        std::vector<std::vector<int>> assoc_neg;
        recon::Result frame_neg = OneCentreFrame(1000.0, 600, &assoc_neg, -1);
        const auto cloud_neg = recon::Triangulate(frame_neg, cal, assoc_neg);
        Check(cloud_neg.empty(), "a negative plane id (dropped by #206) drops the centre");
    }

    // --- the single-plane association helper is shaped to the frame --------
    {
        recon::Result frame;
        frame.rows.resize(3);
        frame.rows[0].centres.resize(2);
        frame.rows[2].centres.resize(1);
        const auto assoc = recon::AssociateSinglePlane(frame);
        Check(assoc.size() == 3 && assoc[0].size() == 2 && assoc[1].empty() && assoc[2].size() == 1,
              "AssociateSinglePlane matches the frame's shape");
        Check(assoc[0][0] == 0 && assoc[2][0] == 0, "AssociateSinglePlane assigns plane 0");
    }

    // The bound is limited by the float32 sub-pixel column the extractor emits
    // (LineCenter.x), not by the geometry: float ulp at u~1000 (~6e-5) scaled
    // by Z/fx is ~1e-5 units. The triangulation itself is exact in double.
    std::printf("[triangulate] worst round-trip error = %.3e units "
                "(synthetic ground truth; float32 pixel-limited)\n",
                worst_error);
    std::printf("[triangulate] %d check(s), %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
