#pragma once

/**
 * @file matrices.h
 * @brief 4x4 matrix type, transform builders, and the matrix-vector product.
 *
 * DEPENDENCY DIRECTION: this includes vectors.h; vectors.h must NEVER include
 * this. transform_Vec4 lives here rather than in vectors.h for exactly that
 * reason -- a matrix-vector product cannot exist without Mat4, so putting it on
 * the vector side would force a cycle. Cycles make compilation silently order-dependent.
 *
 * STORAGE: m[row][col], row-major. Vectors are COLUMN vectors, so a transform
 * is applied as v' = M*v and composition reads right-to-left:
 * multiply(A, B) means "apply B first, then A".
 *
 * CONVENTION: right-handed axes (X right, Y up, Z toward viewer), camera looks
 * down -Z. All angles in RADIANS.
 */

#include "vectors.h"
#include <cstdio>
#include <cmath>

/**
 * @brief 4x4 transform matrix, row-major.
 *
 * 4x4 rather than 3x3 because translation is not a linear operation on 3D
 * points -- it cannot be written as a 3x3 matrix product. Lifting to
 * homogeneous coordinates (appending w) makes translation linear in 4D, which
 * is what lets the entire model->world->eye->clip chain collapse into a single
 * matrix multiply. That collapse is the whole reason GPUs have a vertex stage:
 * one 4x4 multiply per vertex, uniform work, no branches.\\
 * 
 * POD, no constructor — Mat4 m; 
 * is 16 floats of stack garbage, so every builder must write all 16 or zero first.
 */
struct Mat4 { float m[4][4]; };


/**
 * @brief The 4x4 identity matrix.
 *
 * Useful as a safe default and as a test fixture: multiply_matrices(identity(),
 * M) must return M bit-for-bit. Cheap sanity check on the multiply.
 */
inline Mat4 identity() {
    return {{{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}}};
}

/**
 * @brief Matrix product a*b.
 *
 * NOT COMMUTATIVE: a*b != b*a. Order is the single most common source of
 * transform bugs. With column vectors, multiply(a,b) applied to v gives
 * a*(b*v) -- b acts FIRST. So "scale then translate" is
 * multiply(translation, scale), which reads backwards from how you say it.
 */

inline Mat4 multiply_matrices(const Mat4& a, const Mat4& b) {
    Mat4 result;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            result.m[i][j] = 0.0f;
            for (int k = 0; k < 4; ++k) {
                result.m[i][j] += a.m[i][k] * b.m[k][j];
            }
        }
    }
    return result;
}

/**
 * @brief Rotation about the X axis by `angle` radians. Fills all 16 elements.
 *
 * The 2x2 block in the (y,z) sub-space is the standard planar rotation; the x
 * row and column are left as identity because points on the X axis don't move.
 *
 * SIGN VERIFIED: at 90 deg (c=0, s=1) this sends (0,1,0) -> (0,0,1), i.e.
 * +Y -> +Z. Right-hand rule with thumb along +X curls +Y toward +Z. Correct
 * for right-handed axes.
 *
 * Out-parameter rather than a return value, which means `Mat4 r;` followed by a
 * forgotten call leaves r as garbage with no compiler warning. Returning Mat4
 * would make that unrepresentable -- planned change, deliberately deferred so
 * the file split can be verified as a no-op first.
 */

inline void buildRotationMatrix_x(Mat4& rotationMatrix, float angle) {
    float c = std::cos(angle);
    float s = std::sin(angle);

    rotationMatrix.m[0][0] = 1;  rotationMatrix.m[0][1] = 0;  rotationMatrix.m[0][2] = 0;  rotationMatrix.m[0][3] = 0;
    rotationMatrix.m[1][0] = 0;  rotationMatrix.m[1][1] = c;  rotationMatrix.m[1][2] = -s; rotationMatrix.m[1][3] = 0;
    rotationMatrix.m[2][0] = 0;  rotationMatrix.m[2][1] = s;  rotationMatrix.m[2][2] = c;  rotationMatrix.m[2][3] = 0;
    rotationMatrix.m[3][0] = 0;  rotationMatrix.m[3][1] = 0;  rotationMatrix.m[3][2] = 0;  rotationMatrix.m[3][3] = 1;
}

/**
 * @brief Rotation about the Y axis by `angle` radians.
 *
 * NOTE the sign pattern is TRANSPOSED relative to X and Z: +s is at [0][2] and
 * -s at [2][0]. Not a typo. The cyclic order of the axes is x->y->z->x, so the
 * (z,x) plane is traversed in the opposite index order to (y,z) and (x,y).
 * Making this "consistent" with the other two flips the rotation direction.
 *
 * SIGN VERIFIED: at 90 deg this sends (1,0,0) -> (0,0,-1). Right-hand rule,
 * thumb along +Y, curls +Z toward +X, hence +X toward -Z. Correct.
 */
inline void buildRotationMatrix_y(Mat4& rotationMatrix, float angle) {
    float c = std::cos(angle);
    float s = std::sin(angle);

    rotationMatrix.m[0][0] = c;  rotationMatrix.m[0][1] = 0;  rotationMatrix.m[0][2] = s;  rotationMatrix.m[0][3] = 0;
    rotationMatrix.m[1][0] = 0;  rotationMatrix.m[1][1] = 1;  rotationMatrix.m[1][2] = 0;  rotationMatrix.m[1][3] = 0;
    rotationMatrix.m[2][0] = -s; rotationMatrix.m[2][1] = 0;  rotationMatrix.m[2][2] = c;  rotationMatrix.m[2][3] = 0;
    rotationMatrix.m[3][0] = 0;  rotationMatrix.m[3][1] = 0;  rotationMatrix.m[3][2] = 0;  rotationMatrix.m[3][3] = 1;
}

/**
 * @brief Rotation about the Z axis by `angle` radians.
 *
 * SIGN VERIFIED: at 90 deg this sends (1,0,0) -> (0,1,0), +X -> +Y.
 * Counter-clockwise when viewed from +Z looking back at the origin, which is
 * the standard right-handed sense. Correct.
 */
inline void buildRotationMatrix_z(Mat4& rotationMatrix, float angle) {
    float c = std::cos(angle);
    float s = std::sin(angle);

    rotationMatrix.m[0][0] = c;  rotationMatrix.m[0][1] = -s; rotationMatrix.m[0][2] = 0;  rotationMatrix.m[0][3] = 0;
    rotationMatrix.m[1][0] = s;  rotationMatrix.m[1][1] = c;  rotationMatrix.m[1][2] = 0;  rotationMatrix.m[1][3] = 0;
    rotationMatrix.m[2][0] = 0;  rotationMatrix.m[2][1] = 0;  rotationMatrix.m[2][2] = 1;  rotationMatrix.m[2][3] = 0;
    rotationMatrix.m[3][0] = 0;  rotationMatrix.m[3][1] = 0;  rotationMatrix.m[3][2] = 0;  rotationMatrix.m[3][3] = 1;
}

/**
 * @brief Translation by (tx, ty, tz).
 *
 * The offsets sit in the fourth COLUMN, m[0..2][3], which is why they only
 * affect a vector with w=1: each output row picks up m[i][3]*w. A direction
 * with w=0 passes through untranslated. That single fact is why normals are
 * transformed with w=0 -- moving a direction is meaningless, only rotating and
 * scaling it are.
 */

inline void buildTranslationMatrix(Mat4& m, float tx, float ty, float tz)
{
    
    m.m[0][0] = 1; m.m[0][1] = 0; m.m[0][2] = 0; m.m[0][3] = tx;
    m.m[1][0] = 0; m.m[1][1] = 1; m.m[1][2] = 0; m.m[1][3] = ty;
    m.m[2][0] = 0; m.m[2][1] = 0; m.m[2][2] = 1; m.m[2][3] = tz;
    m.m[3][0] = 0; m.m[3][1] = 0; m.m[3][2] = 0; m.m[3][3] = 1;

}

/**
 * @brief Non-uniform scale by (sx, sy, sz). Diagonal matrix.
 *
 * Used for model normalisation (uniform 2/extent), which is why normals need
 * renormalising after transform: a uniform scale multiplies a normal's length
 * by s without changing its direction.
 *
 * Under NON-uniform scale, normals do NOT transform correctly
 * by this matrix -- they need the inverse-transpose, because scaling a surface
 * tilts its normal the opposite way to how it tilts the surface. Currently
 * harmless (all scales here are uniform); becomes a real bug the moment a model
 * is squashed on one axis.
 */
inline void buildScalingMatrix(Mat4& m, float sx, float sy, float sz)
{
    m.m[0][0] = sx; m.m[0][1] = 0;  m.m[0][2] = 0;  m.m[0][3] = 0;
    m.m[1][0] = 0;  m.m[1][1] = sy; m.m[1][2] = 0;  m.m[1][3] = 0;
    m.m[2][0] = 0;  m.m[2][1] = 0;  m.m[2][2] = sz; m.m[2][3] = 0;
    m.m[3][0] = 0;  m.m[3][1] = 0;  m.m[3][2] = 0;  m.m[3][3] = 1;
}
  
/**
 * @brief Perspective projection matrix.
 * @param[out] perspectiveMatrix  Fully overwritten; all 16 elements are set.
 * @param fov         Vertical field of view, RADIANS (call sites pass degrees
 *                    * PI/180 ).
 * @param aspectRatio width/height.
 * @param nearPlane   Distance to near plane, POSITIVE. Must be > 0.
 * @param farPlane    Distance to far plane, POSITIVE.
 *
 * DERIVATION: a point at eye-space depth -z projects onto a screen at distance
 * d by similar triangles: x_screen = d*x/(-z). The division by z is the entire
 * content of perspective, and it is NOT a linear operation, so no 4x4 matrix
 * can perform it. The trick is to have the matrix COPY -z into the output w
 * (that's the -1 at m[3][2]) and let the divide happen afterwards as the
 * perspective divide, in perspectiveTransform below.
 *
 * t = 1/tan(fov/2) is the cotangent, i.e. the distance to a screen of
 * half-height 1. Wider fov -> smaller t -> more of the world squeezed in.
 * Dividing by aspectRatio in m[0][0] stretches the horizontal so pixels stay
 * square on a non-square viewport.
 *
 * The m[2][2], m[2][3] pair maps the depth range [near, far] onto NDC [-1, 1]
 * AFTER the w divide. Because the mapping is in 1/z rather than z, depth
 * precision is heavily front-loaded -- most of the available bits go to
 * geometry near the camera. That is z-fighting's root cause and the reason
 * pushing `near` outward is the standard fix. Directly relevant to RTL rasterizer:
 * a fixed-point z-buffer must budget its bits around this non-uniformity.
 *
 * ONLY 6 OF 16 ELEMENTS ARE MEANINGFUL, so this builder MUST
 * zero the matrix first. Removing that loop leaves 10 elements as stack
 * garbage.
 */
inline void buildPerspectiveMatrix(Mat4& perspectiveMatrix, float fov, float aspectRatio, float nearPlane, float farPlane) {
    float t = 1.0f / std::tan(fov / 2.0f);

    for (int i = 0; i<4; ++i) {
        for (int j = 0; j<4; ++j) {
            perspectiveMatrix.m[i][j] = 0.0f;
        }
    }

    perspectiveMatrix.m[0][0] = t / aspectRatio;
    perspectiveMatrix.m[1][1] = t;
    perspectiveMatrix.m[2][2] = -(farPlane + nearPlane) / (farPlane - nearPlane);
    perspectiveMatrix.m[2][3] = -(2.0f * farPlane * nearPlane) / (farPlane - nearPlane);
    perspectiveMatrix.m[3][2] = -1.0f;
}

/**
 * @brief Matrix-vector product mat * vec, treating vec as a column vector.
 *
 * 16 multiplies, 12 adds. This is the per-vertex workload of the geometry
 * stage: fixed cost, no branches, no memory dependencies beyond the fetch.
 
 * Declared ABOVE perspectiveTransform because C++ resolves names top-to-bottom
 * within a translation unit -- a call to a name not yet declared is an error.
 */

inline Vec4 transform_Vec4(const Mat4& mat, const Vec4& vec) {
    Vec4 result;
    result.x = mat.m[0][0] * vec.x + mat.m[0][1] * vec.y + mat.m[0][2] * vec.z + mat.m[0][3] * vec.w;
    result.y = mat.m[1][0] * vec.x + mat.m[1][1] * vec.y + mat.m[1][2] * vec.z + mat.m[1][3] * vec.w;
    result.z = mat.m[2][0] * vec.x + mat.m[2][1] * vec.y + mat.m[2][2] * vec.z + mat.m[2][3] * vec.w;
    result.w = mat.m[3][0] * vec.x + mat.m[3][1] * vec.y + mat.m[3][2] * vec.z + mat.m[3][3] * vec.w;
    return result;
}


/**
 * @brief Apply m to vertex, then perform the perspective divide. Returns NDC.
 *
 * Two distinct steps that are easy to conflate:
 *   1. transform to CLIP space (a 4D homogeneous point, w != 1)
 *   2. divide x,y,z by w -> NORMALISED DEVICE COORDINATES, all in [-1, 1]
 *
 * Step 2 is where perspective actually happens; the matrix only set it up.
 * Note this is a DIVIDE per vertex -- the one non-uniform-cost operation in an
 * otherwise pure multiply-add pipeline, and correspondingly expensive in
 * hardware (a reciprocal unit, not a DSP slice). Computing 1/w once and
 * multiplying three times, as done here, is the standard trade: 1 divide + 3
 * multiplies beats 3 divides.
 *
 * The 1e-8f guard catches w ~= 0, which means the vertex is on or behind the
 * eye plane where projection is undefined (it would map to infinity). Returning
 * the origin is a placeholder, NOT correct behaviour -- the real fix is
 * near-plane clipping before the divide. Currently unhit because the camera
 * sits outside the model; will break the moment the camera moves inside one.
 */

inline Vec3 perspectiveTransform(const Vec4& vertex, const Mat4& m) {
    Vec4 clip = transform_Vec4(m, vertex);
    float inv_w = (std::fabs(clip.w) > 1e-8f) ? 1.0f / clip.w : 0.0f;
    return { clip.x * inv_w, clip.y * inv_w, clip.z * inv_w };
}


/**
 * @brief Debug dump: one matrix row per line, fixed width so columns align.
 *
 * printf rather than std::cout to keep \<iostream\> out of a widely-included
 * header (it is large and injects a static initialiser into every TU).
 */

inline void printMatrix(const Mat4& mat) {
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            std::printf("%f",mat.m[i][j]);
        }
        std::printf("\n");
    }
}