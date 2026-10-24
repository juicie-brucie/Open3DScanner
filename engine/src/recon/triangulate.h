#pragma once
// Triangulate laser-line centres into a 3-D point cloud (Phase C, WO-2).
//
// Textbook structured-light triangulation: a sub-pixel laser-line centre (a
// pixel u,v) back-projects to a camera ray, and the 3-D point is where that
// ray meets the laser plane the line belongs to. The device projects several
// laser lines at once (the recorded IR frames carry a multi-line pattern, and
// the #204 calibration draft decodes ~29 planes in 3 groups), so the
// calibration is a SET of planes and each centre must name the plane it
// belongs to. That association is the emitter-line selection stage (#206); it
// is injected here as input and never decided in this module.
//
// Clean-room: pure geometry from our own specs. No vendor ABI, no decompiled
// code, and no calibration values baked in — the Calibration is supplied by
// the caller (synthetic in the tests; recon's decoded plane set when #204
// lands). The plane/calibration boundary stays explicit.

#include <cstddef>
#include <vector>

#include "recon/laser_line.h"

namespace openscanner::recon {

// Pinhole camera intrinsics, in pixels. Lens distortion is out of scope for
// this seed; a pre-undistortion step would feed undistorted (u,v) in without
// changing the geometry below.
struct CameraIntrinsics {
    double fx = 0.0;
    double fy = 0.0;
    double cx = 0.0;
    double cy = 0.0;

    bool valid() const { return fx > 0.0 && fy > 0.0; }
};

// A laser plane in the camera frame: a*X + b*Y + c*Z + d = 0, with (a,b,c) the
// normal (it need not be unit length — the intersection uses only a ratio). A
// point lies on the plane when the expression is zero.
//
// recon's decoded form (#204/#508) is `n·X = d` with unit n and d in mm, so a
// loader maps a decoded plane to this struct as {a,b,c} = n, d = -d_recon. The
// decoded values stay recon's; this struct only receives them (the boundary
// the caller keeps explicit).
struct LaserPlane {
    double a = 0.0;
    double b = 0.0;
    double c = 0.0;
    double d = 0.0;
};

// The calibration: the camera and the set of laser planes. `planes` is indexed
// by plane id, and a centre's association indexes into it. The triangulation
// needs only the flat plane list; the #204 draft's 3-group structure, if the
// caller tracks it, is bookkeeping above this struct.
struct Calibration {
    CameraIntrinsics camera;
    std::vector<LaserPlane> planes;
};

struct Point3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

// One triangulated point with the measurement it came from, so a caller can
// score or filter the cloud without re-joining to the centres.
struct CloudPoint {
    Point3 position;
    int plane = -1;       // the plane id used
    int row = 0;          // the source centre's row (v)
    float column = 0.0f;  // the source centre's sub-pixel column (u)
    float confidence = 0.0f;
};

// The per-centre plane association, built by the emitter-line selection stage
// (#206). Shaped to match a laser_line::Result: assoc[y][k] is the plane id for
// rows[y].centres[k], or a negative value to drop that centre.
using FrameAssociation = std::vector<std::vector<int>>;

// An association that assigns every centre to plane 0 — the trivial one-plane
// case, used for a single-line self-consistency pass and by the tests. It is
// NOT the real multi-line association (#206 provides that); it is shaped to
// match `frame`.
FrameAssociation AssociateSinglePlane(const Result& frame);

// Back-project one pixel to the 3-D point where its camera ray meets `plane`.
// `*ok` is set false (and the return is zero) when the intrinsics are invalid,
// the ray is parallel to the plane (no intersection), or the intersection is
// at or behind the camera (z <= 0).
Point3 BackprojectPixelToPlane(double u, double v, const CameraIntrinsics& camera,
                               const LaserPlane& plane, bool* ok);

// Triangulate a frame's centres with a per-centre plane association. A centre
// whose association is out of range, or whose ray misses the plane, is skipped
// (so the cloud can be smaller than the centre count). Deterministic;
// allocation is bounded to the output.
std::vector<CloudPoint> Triangulate(const Result& frame, const Calibration& calibration,
                                    const FrameAssociation& association);

}  // namespace openscanner::recon
