#include "openscanner/mesh_tools.h"

#include <Eigen/Core>
#include <igl/decimate.h>
#include <manifold/manifold.h>

#include <stdexcept>

namespace openscanner::mesh {

namespace {

manifold::MeshGL64 to_manifold_mesh(const MeshBuffers& mesh) {
    manifold::MeshGL64 m;
    m.numProp = 3;
    m.vertProperties = mesh.vertices;
    m.triVerts.reserve(mesh.triangles.size());
    for (uint32_t idx : mesh.triangles) {
        m.triVerts.push_back(static_cast<uint64_t>(idx));
    }
    return m;
}

MeshBuffers from_manifold_mesh(const manifold::Manifold& m) {
    const manifold::MeshGL64 out = m.GetMeshGL64();
    MeshBuffers result;
    result.vertices = out.vertProperties;
    result.triangles.reserve(out.triVerts.size());
    for (uint64_t idx : out.triVerts) {
        result.triangles.push_back(static_cast<uint32_t>(idx));
    }
    return result;
}

}  // namespace

MeshBuffers make_box(double size_x, double size_y, double size_z) {
    const manifold::Manifold box =
        manifold::Manifold::Cube(manifold::vec3(size_x, size_y, size_z), /*center=*/true);
    return from_manifold_mesh(box);
}

MeshBuffers mesh_boolean(const MeshBuffers& a, const MeshBuffers& b, BooleanOp op) {
    const manifold::Manifold ma(to_manifold_mesh(a));
    const manifold::Manifold mb(to_manifold_mesh(b));
    if (ma.Status() != manifold::Manifold::Error::NoError) {
        throw std::invalid_argument("mesh_boolean: first operand is not a valid manifold mesh");
    }
    if (mb.Status() != manifold::Manifold::Error::NoError) {
        throw std::invalid_argument("mesh_boolean: second operand is not a valid manifold mesh");
    }

    manifold::OpType manifold_op;
    switch (op) {
        case BooleanOp::kUnion:
            manifold_op = manifold::OpType::Add;
            break;
        case BooleanOp::kDifference:
            manifold_op = manifold::OpType::Subtract;
            break;
        case BooleanOp::kIntersection:
            manifold_op = manifold::OpType::Intersect;
            break;
        default:
            throw std::invalid_argument("mesh_boolean: unknown BooleanOp");
    }

    const manifold::Manifold result = ma.Boolean(mb, manifold_op);
    return from_manifold_mesh(result);
}

MeshBuffers decimate(const MeshBuffers& mesh, int target_triangle_count) {
    if (target_triangle_count <= 0) {
        throw std::invalid_argument("decimate: target_triangle_count must be positive");
    }

    const auto vertex_count = static_cast<Eigen::Index>(mesh.vertex_count());
    const auto triangle_count = static_cast<Eigen::Index>(mesh.triangle_count());

    Eigen::MatrixXd V(vertex_count, 3);
    for (Eigen::Index i = 0; i < vertex_count; ++i) {
        V(i, 0) = mesh.vertices[3 * i + 0];
        V(i, 1) = mesh.vertices[3 * i + 1];
        V(i, 2) = mesh.vertices[3 * i + 2];
    }

    Eigen::MatrixXi F(triangle_count, 3);
    for (Eigen::Index i = 0; i < triangle_count; ++i) {
        F(i, 0) = static_cast<int>(mesh.triangles[3 * i + 0]);
        F(i, 1) = static_cast<int>(mesh.triangles[3 * i + 1]);
        F(i, 2) = static_cast<int>(mesh.triangles[3 * i + 2]);
    }

    Eigen::MatrixXd U;
    Eigen::MatrixXi G;
    Eigen::VectorXi J;
    Eigen::VectorXi I;
    // Not checking the return value: decimate() returns false when it can't
    // reach target_triangle_count exactly (e.g. not enough collapsible
    // edges), but still leaves U/G as a valid, maximally-decimated mesh.
    igl::decimate(V, F, target_triangle_count, /*block_intersections=*/false, U, G, J, I);

    MeshBuffers result;
    result.vertices.reserve(static_cast<size_t>(U.rows()) * 3);
    for (Eigen::Index i = 0; i < U.rows(); ++i) {
        result.vertices.push_back(U(i, 0));
        result.vertices.push_back(U(i, 1));
        result.vertices.push_back(U(i, 2));
    }
    result.triangles.reserve(static_cast<size_t>(G.rows()) * 3);
    for (Eigen::Index i = 0; i < G.rows(); ++i) {
        result.triangles.push_back(static_cast<uint32_t>(G(i, 0)));
        result.triangles.push_back(static_cast<uint32_t>(G(i, 1)));
        result.triangles.push_back(static_cast<uint32_t>(G(i, 2)));
    }
    return result;
}

}  // namespace openscanner::mesh
