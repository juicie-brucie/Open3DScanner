#pragma once
// A compact binary encoding of a triangle mesh for the live ws channel
// (WO-B2): the bridge that lets the live FastSurface stream to the viewport
// over the same WsBroadcastServer::BroadcastBinary path the point frames use.
// A magic distinguishes a mesh frame from a point frame so the UI can switch
// on it; float32 vertices + uint32 indices keep it small for per-tick sends.
//
// Layout (little-endian):
//   magic 'OMSH' (4 bytes) | version u32 = 1 | vertex_count u32 |
//   triangle_count u32 | vertices: vertex_count * 3 float32 |
//   triangles: triangle_count * 3 uint32
//
// Pure/dependency-free (no libigl); the UI decode is the mirror of this.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "openscanner/mesh_tools.h"  // MeshBuffers

namespace openscanner::recon {

// Encode a mesh to one binary frame (see the layout above).
std::vector<uint8_t> EncodeMeshFrame(const openscanner::mesh::MeshBuffers& mesh);

// Decode a binary frame back to a mesh. False (and *out cleared) on a bad
// magic/version, a truncated buffer, or an index out of range.
bool DecodeMeshFrame(const uint8_t* data, std::size_t size, openscanner::mesh::MeshBuffers* out);

}  // namespace openscanner::recon
