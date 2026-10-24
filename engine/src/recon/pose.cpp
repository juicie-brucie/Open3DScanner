#include "recon/pose.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace openscanner::recon {
namespace {

// Cyclic Jacobi eigensolver for a small symmetric matrix (used at n = 3 and
// n = 4). `a` is row-major n*n and is overwritten; eigenvalues land in w, and
// the eigenvectors are the columns of V (row-major n*n). Not sorted. Jacobi is
// chosen for its accuracy on tiny symmetric matrices and because it is a fixed,
// allocation-free kernel — in keeping with the module's "adaptability" goal
// (easy to relocate or replace), see pose.h.
void JacobiEigen(double* a, int n, double* w, double* V) {
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j) V[i * n + j] = (i == j) ? 1.0 : 0.0;
    for (int sweep = 0; sweep < 100; ++sweep) {
        double off = 0.0;
        for (int p = 0; p < n; ++p)
            for (int q = p + 1; q < n; ++q) off += a[p * n + q] * a[p * n + q];
        if (off < 1e-30) break;
        for (int p = 0; p < n; ++p) {
            for (int q = p + 1; q < n; ++q) {
                const double apq = a[p * n + q];
                if (std::fabs(apq) < 1e-300) continue;
                // The rotation that zeroes a[p][q]: tan(2*phi) = 2*a_pq/(a_qq - a_pp).
                const double phi = 0.5 * std::atan2(2.0 * apq, a[q * n + q] - a[p * n + p]);
                const double c = std::cos(phi), s = std::sin(phi);
                for (int k = 0; k < n; ++k) {  // columns p,q of a
                    const double akp = a[k * n + p], akq = a[k * n + q];
                    a[k * n + p] = c * akp - s * akq;
                    a[k * n + q] = s * akp + c * akq;
                }
                for (int k = 0; k < n; ++k) {  // rows p,q of a
                    const double apk = a[p * n + k], aqk = a[q * n + k];
                    a[p * n + k] = c * apk - s * aqk;
                    a[q * n + k] = s * apk + c * aqk;
                }
                for (int k = 0; k < n; ++k) {  // accumulate into V
                    const double vkp = V[k * n + p], vkq = V[k * n + q];
                    V[k * n + p] = c * vkp - s * vkq;
                    V[k * n + q] = s * vkp + c * vkq;
                }
            }
        }
    }
    for (int i = 0; i < n; ++i) w[i] = a[i * n + i];
}

}  // namespace

Point3 Apply(const Pose& pose, const Point3& p) {
    return Point3{pose.r[0] * p.x + pose.r[1] * p.y + pose.r[2] * p.z + pose.t[0],
                  pose.r[3] * p.x + pose.r[4] * p.y + pose.r[5] * p.z + pose.t[1],
                  pose.r[6] * p.x + pose.r[7] * p.y + pose.r[8] * p.z + pose.t[2]};
}

Pose Compose(const Pose& a, const Pose& b) {
    Pose c;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            double s = 0.0;
            for (int k = 0; k < 3; ++k) s += a.r[i * 3 + k] * b.r[k * 3 + j];
            c.r[i * 3 + j] = s;
        }
    for (int i = 0; i < 3; ++i) {
        double s = 0.0;
        for (int k = 0; k < 3; ++k) s += a.r[i * 3 + k] * b.t[k];
        c.t[i] = s + a.t[i];
    }
    return c;
}

Pose Invert(const Pose& pose) {
    Pose inv;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) inv.r[i * 3 + j] = pose.r[j * 3 + i];  // R^T
    for (int i = 0; i < 3; ++i) {
        double s = 0.0;
        for (int k = 0; k < 3; ++k) s += inv.r[i * 3 + k] * pose.t[k];
        inv.t[i] = -s;  // -R^T t
    }
    return inv;
}

PoseEstimate KabschEstimator::Estimate(const std::vector<Correspondence>& pairs) const {
    PoseEstimate out;

    // Weighted centroids and the used count.
    double wsum = 0.0;
    Point3 fc{}, tc{};
    int used = 0;
    for (const Correspondence& c : pairs) {
        if (!(c.weight > 0.0)) continue;
        ++used;
        wsum += c.weight;
        fc.x += c.weight * c.from.x; fc.y += c.weight * c.from.y; fc.z += c.weight * c.from.z;
        tc.x += c.weight * c.to.x;   tc.y += c.weight * c.to.y;   tc.z += c.weight * c.to.z;
    }
    out.used = used;
    if (used < 3 || !(wsum > 0.0)) return out;  // need 3 pairs for a unique rigid pose
    fc.x /= wsum; fc.y /= wsum; fc.z /= wsum;
    tc.x /= wsum; tc.y /= wsum; tc.z /= wsum;

    // Cross-covariance M = Σ w (from-fc)(to-tc)^T, and the from-covariance C
    // (for the degeneracy test).
    double M[9] = {0};
    double C[9] = {0};
    for (const Correspondence& c : pairs) {
        if (!(c.weight > 0.0)) continue;
        const double ax = c.from.x - fc.x, ay = c.from.y - fc.y, az = c.from.z - fc.z;
        const double bx = c.to.x - tc.x, by = c.to.y - tc.y, bz = c.to.z - tc.z;
        const double w = c.weight;
        M[0] += w * ax * bx; M[1] += w * ax * by; M[2] += w * ax * bz;
        M[3] += w * ay * bx; M[4] += w * ay * by; M[5] += w * ay * bz;
        M[6] += w * az * bx; M[7] += w * az * by; M[8] += w * az * bz;
        C[0] += w * ax * ax; C[1] += w * ax * ay; C[2] += w * ax * az;
        C[4] += w * ay * ay; C[5] += w * ay * az; C[8] += w * az * az;
    }
    C[3] = C[1]; C[6] = C[2]; C[7] = C[5];

    // Degeneracy: the from-points must span at least two dimensions — a
    // collinear or coincident set leaves the rotation about that line/point
    // undetermined.
    {
        double a3[9];
        for (int i = 0; i < 9; ++i) a3[i] = C[i];
        double w3[3], v3[9];
        JacobiEigen(a3, 3, w3, v3);
        double eig[3] = {w3[0], w3[1], w3[2]};
        std::sort(eig, eig + 3);  // ascending; eig[2] largest, eig[1] second
        if (eig[2] < 1e-12 || eig[1] < 1e-9 * eig[2]) return out;
    }

    // Horn's quaternion method: the unit quaternion maximizing q^T N q (the
    // largest-eigenvalue eigenvector of the 4x4 N built from M) is the optimal
    // rotation from the from-set to the to-set. A quaternion is always a proper
    // rotation, so no reflection case can arise — a planar set is fine.
    const double Sxx = M[0], Sxy = M[1], Sxz = M[2];
    const double Syx = M[3], Syy = M[4], Syz = M[5];
    const double Szx = M[6], Szy = M[7], Szz = M[8];
    double N[16] = {
        Sxx + Syy + Szz, Syz - Szy,        Szx - Sxz,        Sxy - Syx,
        Syz - Szy,       Sxx - Syy - Szz,  Sxy + Syx,        Szx + Sxz,
        Szx - Sxz,       Sxy + Syx,        -Sxx + Syy - Szz, Syz + Szy,
        Sxy - Syx,       Szx + Sxz,        Syz + Szy,        -Sxx - Syy + Szz};
    double wN[4], vN[16];
    JacobiEigen(N, 4, wN, vN);
    int best = 0;
    for (int i = 1; i < 4; ++i)
        if (wN[i] > wN[best]) best = i;
    double qw = vN[0 * 4 + best], qx = vN[1 * 4 + best], qy = vN[2 * 4 + best], qz = vN[3 * 4 + best];
    const double qn = std::sqrt(qw * qw + qx * qx + qy * qy + qz * qz);
    if (qn < 1e-12) return out;
    qw /= qn; qx /= qn; qy /= qn; qz /= qn;

    Pose pose;
    pose.r[0] = 1 - 2 * (qy * qy + qz * qz); pose.r[1] = 2 * (qx * qy - qw * qz); pose.r[2] = 2 * (qx * qz + qw * qy);
    pose.r[3] = 2 * (qx * qy + qw * qz);     pose.r[4] = 1 - 2 * (qx * qx + qz * qz); pose.r[5] = 2 * (qy * qz - qw * qx);
    pose.r[6] = 2 * (qx * qz - qw * qy);     pose.r[7] = 2 * (qy * qz + qw * qx);     pose.r[8] = 1 - 2 * (qx * qx + qy * qy);
    pose.t[0] = tc.x - (pose.r[0] * fc.x + pose.r[1] * fc.y + pose.r[2] * fc.z);
    pose.t[1] = tc.y - (pose.r[3] * fc.x + pose.r[4] * fc.y + pose.r[5] * fc.z);
    pose.t[2] = tc.z - (pose.r[6] * fc.x + pose.r[7] * fc.y + pose.r[8] * fc.z);

    double sse = 0.0;
    for (const Correspondence& c : pairs) {
        if (!(c.weight > 0.0)) continue;
        const Point3 r = Apply(pose, c.from);
        const double dx = r.x - c.to.x, dy = r.y - c.to.y, dz = r.z - c.to.z;
        sse += dx * dx + dy * dy + dz * dz;
    }
    out.rms_residual = std::sqrt(sse / static_cast<double>(used));
    out.pose = pose;
    out.ok = true;
    return out;
}

}  // namespace openscanner::recon
