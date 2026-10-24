#include "recon/mesh_compare.h"

#include <Eigen/Dense>
#include <igl/hausdorff.h>
#include <igl/point_mesh_squared_distance.h>
#include <manifold/manifold.h>

#include <cmath>
#include <cstdint>

namespace openscanner::recon {
namespace {

void ToEigen(const openscanner::mesh::MeshBuffers& mesh, Eigen::MatrixXd* V, Eigen::MatrixXi* F) {
    const auto nv = static_cast<Eigen::Index>(mesh.vertex_count());
    const auto nf = static_cast<Eigen::Index>(mesh.triangle_count());
    V->resize(nv, 3);
    for (Eigen::Index i = 0; i < nv; ++i) {
        (*V)(i, 0) = mesh.vertices[3 * i + 0];
        (*V)(i, 1) = mesh.vertices[3 * i + 1];
        (*V)(i, 2) = mesh.vertices[3 * i + 2];
    }
    F->resize(nf, 3);
    for (Eigen::Index i = 0; i < nf; ++i) {
        (*F)(i, 0) = static_cast<int>(mesh.triangles[3 * i + 0]);
        (*F)(i, 1) = static_cast<int>(mesh.triangles[3 * i + 1]);
        (*F)(i, 2) = static_cast<int>(mesh.triangles[3 * i + 2]);
    }
}

}  // namespace

bool IsWatertight(const openscanner::mesh::MeshBuffers& mesh) {
    if (mesh.triangle_count() == 0) return false;
    manifold::MeshGL64 gl;
    gl.numProp = 3;
    gl.vertProperties = mesh.vertices;
    gl.triVerts.reserve(mesh.triangles.size());
    for (uint32_t idx : mesh.triangles) gl.triVerts.push_back(static_cast<uint64_t>(idx));
    // Manifold only represents valid closed 2-manifolds; a mesh with boundary
    // or non-manifold edges constructs with a non-NoError status.
    const manifold::Manifold m(gl);
    return m.Status() == manifold::Manifold::Error::NoError && m.NumTri() > 0;
}

MeshMetrics CompareMeshes(const openscanner::mesh::MeshBuffers& a,
                          const openscanner::mesh::MeshBuffers& b) {
    MeshMetrics m;
    m.tris_a = a.triangle_count();
    m.tris_b = b.triangle_count();
    if (a.triangle_count() == 0 || b.triangle_count() == 0) return m;

    Eigen::MatrixXd VA, VB;
    Eigen::MatrixXi FA, FB;
    ToEigen(a, &VA, &FA);
    ToEigen(b, &VB, &FB);

    double d = 0.0;
    igl::hausdorff(VA, FA, VB, FB, d);
    m.hausdorff = d;

    // RMS / max distance of A's vertices to B's surface.
    Eigen::VectorXd sqrD;
    Eigen::VectorXi I;
    Eigen::MatrixXd C;
    igl::point_mesh_squared_distance(VA, VB, FB, sqrD, I, C);
    double sum = 0.0, worst = 0.0;
    for (Eigen::Index i = 0; i < sqrD.size(); ++i) {
        sum += sqrD(i);
        worst = std::max(worst, sqrD(i));
    }
    m.rms_surface = std::sqrt(sum / static_cast<double>(sqrD.size()));
    m.max_surface = std::sqrt(worst);

    m.watertight_a = IsWatertight(a);
    m.watertight_b = IsWatertight(b);
    m.valid = true;
    return m;
}

}  // namespace openscanner::recon
