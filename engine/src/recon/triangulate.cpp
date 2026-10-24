#include "recon/triangulate.h"

#include <cmath>
#include <cstddef>

namespace openscanner::recon {

FrameAssociation AssociateSinglePlane(const Result& frame) {
    FrameAssociation association;
    association.resize(frame.rows.size());
    for (std::size_t y = 0; y < frame.rows.size(); ++y) {
        association[y].assign(frame.rows[y].centres.size(), 0);
    }
    return association;
}

Point3 BackprojectPixelToPlane(double u, double v, const CameraIntrinsics& camera,
                               const LaserPlane& plane, bool* ok) {
    const auto fail = [ok]() {
        if (ok) *ok = false;
        return Point3{};
    };
    if (!camera.valid()) return fail();

    // The camera ray through the pixel, with z fixed to 1: a point on the ray
    // is t * dir for t > 0, so the intersection's depth is t itself.
    const double dir_x = (u - camera.cx) / camera.fx;
    const double dir_y = (v - camera.cy) / camera.fy;
    const double dir_z = 1.0;

    // a*(t dir) + d = 0  ->  t = -d / (a dir_x + b dir_y + c dir_z).
    const double denom = plane.a * dir_x + plane.b * dir_y + plane.c * dir_z;
    constexpr double kParallelEps = 1e-9;
    if (std::fabs(denom) < kParallelEps) return fail();  // ray parallel to plane

    const double t = -plane.d / denom;
    if (!(t > 0.0)) return fail();  // behind the camera (also rejects NaN)

    if (ok) *ok = true;
    return Point3{t * dir_x, t * dir_y, t * dir_z};
}

std::vector<CloudPoint> Triangulate(const Result& frame, const Calibration& calibration,
                                    const FrameAssociation& association) {
    std::vector<CloudPoint> cloud;
    if (!calibration.camera.valid() || calibration.planes.empty()) return cloud;

    cloud.reserve(static_cast<std::size_t>(frame.centre_count));
    const int plane_count = static_cast<int>(calibration.planes.size());

    for (std::size_t y = 0; y < frame.rows.size(); ++y) {
        const RowResult& row = frame.rows[y];
        // An association missing this row (shorter than the frame) drops it.
        const bool have_row = y < association.size();
        for (std::size_t k = 0; k < row.centres.size(); ++k) {
            const int plane_id = (have_row && k < association[y].size()) ? association[y][k] : -1;
            if (plane_id < 0 || plane_id >= plane_count) continue;

            const LineCenter& centre = row.centres[k];
            bool ok = false;
            const Point3 position = BackprojectPixelToPlane(centre.x, centre.y, calibration.camera,
                                                            calibration.planes[plane_id], &ok);
            if (!ok) continue;

            CloudPoint point;
            point.position = position;
            point.plane = plane_id;
            point.row = centre.y;
            point.column = centre.x;
            point.confidence = centre.confidence;
            cloud.push_back(point);
        }
    }
    return cloud;
}

}  // namespace openscanner::recon
