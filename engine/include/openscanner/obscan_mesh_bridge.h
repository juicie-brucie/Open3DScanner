#pragma once

// Bridges the captured-data path (ObScanTriangleMesh / ObScanPointClouds, the
// structs recovered from the vendor SDK in obscan_api.h) to the mesh-editing
// toolbox (openscanner::mesh::MeshBuffers, see mesh/include/openscanner/mesh_tools.h).
//
// This is the connective tissue the rest of the "data processing path" (scan
// capture -> denoise/fuse -> edit -> export/render) actually runs through:
// obscan_scan_get_raw_cloud / obscan_scan_get_realtime_model_mesh hand back
// vendor-shaped structs with vendor-owned buffers; everything downstream
// (mesh/, future denoise/fusion code, the UI's WebSocket feed) should work in
// terms of the plain, owned buffer types defined here and in mesh_tools.h
// instead of holding onto those vendor pointers.
//
// Struct field offsets for ObScanTriangleMesh/ObScanPointClouds are marked
// "not yet independently confirmed via disassembly" in obscan_api.h — these
// converters assume the layout documented there is correct. If live capture
// later reveals a different layout, this is the one place that needs fixing
// to keep everything downstream working.

#include <vector>

#include "openscanner/mesh_tools.h"
#include "openscanner/obscan_api.h"

namespace openscanner {

// A captured raw point cloud, copied out of vendor-owned memory into buffers
// this process owns. Positions only for now — normals/intensity from
// ObScanPoint and ObScanNormal are available on the source struct but not
// yet consumed by anything downstream (see
// docs/research/point-cloud-denoising-and-fusion.md: a denoise/fusion step
// needs normals directly, this is deliberately not losing that data at the
// SDK boundary, just not threading it through this particular buffer yet).
struct CapturedPointCloud {
    std::vector<double> positions;  // size = 3 * point_count, xyzxyzxyz...

    [[nodiscard]] size_t point_count() const { return positions.size() / 3; }
};

// Converts a live/preview reconstructed mesh (obscan_scan_get_realtime_model_mesh)
// into mesh::MeshBuffers, ready for mesh/'s boolean/decimate tools. Copies
// all data immediately; does not retain the source struct's pointers.
// Throws std::invalid_argument if a non-zero count comes with a null buffer,
// or a triangle indexes past num_vertices.
mesh::MeshBuffers from_obscan_mesh(const ObScanTriangleMesh& captured);

// Converts a raw point cloud (obscan_scan_get_raw_cloud) into a plain,
// owned position buffer. Copies all data immediately.
// Throws std::invalid_argument if num_points > 0 but points is null.
CapturedPointCloud from_obscan_point_cloud(const ObScanPointClouds& captured);

}  // namespace openscanner
