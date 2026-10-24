#pragma once
// A/B compare two point clouds (Phase C, WO-2): our cloud against a reference
// (the vendor's cloud for the same scene). This is the first stage that scores
// "own the math" against the vendor's own output.
//
// Metrics: the two point counts, the RMS / mean / max nearest-neighbour
// distance from A to B, and coverage — the fraction of B that has an A point
// within `tau`. A uniform voxel grid over B, searched in expanding shells,
// makes each nearest-neighbour query fast and exact, so a small cloud of ours
// can be scored against a vendor cloud of millions of points.
//
// Pure: no I/O, deterministic. The reference's provenance and the paired
// recording are the caller's problem; this only scores two clouds in whatever
// common frame and units the caller has already put them in.

#include <cstddef>
#include <vector>

#include "recon/triangulate.h"

namespace openscanner::recon {

struct CloudMetrics {
    std::size_t count_a = 0;
    std::size_t count_b = 0;
    double rms_nn = 0.0;   // RMS over A of the distance to the nearest B point
    double mean_nn = 0.0;  // mean over A of that distance
    double max_nn = 0.0;   // worst over A
    double coverage = 0.0;  // fraction of B within tau of some A point, 0..1
    bool valid = false;     // false when either cloud is empty, or tau <= 0
};

// Score cloud A (ours) against reference cloud B. `tau` is the coverage radius
// in the clouds' own units and must be > 0. Either cloud empty, or tau <= 0,
// gives valid = false.
CloudMetrics CompareClouds(const std::vector<Point3>& a, const std::vector<Point3>& b, double tau);

// Pull the positions out of a triangulated cloud, for CompareClouds.
std::vector<Point3> Positions(const std::vector<CloudPoint>& cloud);

}  // namespace openscanner::recon
