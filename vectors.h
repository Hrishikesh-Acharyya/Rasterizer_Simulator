#pragma once 
/* #pragma once: include guard. The preprocessor pastes a header's text
 verbatim at each #include. Without this, a header reached twice in one
 translation unit would redefine its structs -> compile error. This has no
 effect ACROSS translation units.*/

 /**
 * @file vec.h
 * @brief Vector types and pure vector arithmetic. Base of the dependency graph.
 *
 * This header depends on NOTHING else in the project. Everything else may
 * include it; it may include nothing back. That one-directional rule keeps the
 * include graph a DAG, which is what makes each .cpp compilable and testable in
 * isolation.
 *
 * WHAT BELONGS HERE: types and operations that are meaningful without knowing
 * anything about pixels, triangles, or files.
 * WHAT DOES NOT: RGB and screenVertex (pixel/raster concerns), Mat4 and
 * transform (matrices.h -- a matrix-vector product cannot exist without Mat4,
 * so it lives on the matrix side; putting it here would force vec.h to include
 * matrices.h and create a cycle).
 *
 * CONVENTION: right-handed axes. X right, Y up, Z toward the viewer.
 * Camera looks down -Z.
 */



#include <cmath>
#include <cstdio>

/**
 * @brief Pi as a typed compile-time constant.
 *
 * constexpr, not #define. A macro is blind text substitution performed before
 * the compiler runs: no type, no scope, invisible to the debugger, and it
 * rewrites every token named PI in every file that transitively includes this
 * one -- including struct members and other people's headers.
 * constexpr is a real typed constant that the compiler folds to the same
 * immediate value. Same performance, none of the hazards.
 */

constexpr float PI = 3.14159265358979f;

/**
 * @brief 3-component vector. Position, direction, or normal depending on use.
 *
 * Plain struct, public members, no constructors or methods. Deliberate: this is
 * a POD (plain old data), so it can be memcpy'd, brace-initialised {x,y,z}, and
 * held in a std::vector with a predictable flat memory layout. That layout is
 * the point -- it mirrors what a hardware vertex fetch would see, and it's what
 * makes the eventual fixed-point port a type change rather than a rewrite.
 */
struct Vec3 { float x,y,z; };

/**
 * @brief 4-component homogeneous vector.
 *
 * The w component distinguishes positions (w=1, affected by translation) from
 * directions (w=0, immune to translation). This is why normals are transformed
 * with w=0: translating a direction is meaningless. See the transform chain in
 * matrices.h.
 */
struct Vec4 { float x,y,z,w;};


/**
 * @brief Component-wise a - b.
 * @return The vector from b to a.
 *
 * Used to build triangle edge vectors before the cross product:
 * edge1 = B - A, edge2 = C - A.
 */
inline Vec3 subtract_Vec3(const Vec3& a, const Vec3& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

/**
 * @brief Component-wise a + b.
 *
 * Used to accumulate face normals onto shared vertices during the
 * smooth-normal pass.
 */
inline Vec3 add_Vec3(const Vec3& v1,const Vec3& v2)
{
  return{v1.x+v2.x,v1.y+v2.y, v1.z+v2.z};
}

 /**
 * @brief Cross product a x b.
 * @return A vector perpendicular to both, with |a x b| = |a||b|sin(theta),
 *         i.e. twice the area of the triangle they span.
 *
 * ORDER MATTERS: b x a = -(a x b). With right-handed axes and
 * counter-clockwise winding, cross(B-A, C-A) gives the OUTWARD face normal.
 * Reverse the winding and the normal flips -- this is the same sign that
 * backface culling and the edge function key off.
 *
 * The magnitude carrying triangle area is why face normals are accumulated
 * UNNORMALISED and only normalised at the end: large triangles then contribute
 * proportionally more to a shared vertex's normal, which is the correct
 * area-weighted average.
 */

inline Vec3 cross_Vec3(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

/**
 * @brief Dot product a . b.
 * @return |a||b|cos(theta) between the two vectors.
 *
 * For two UNIT vectors this is exactly cos(theta), which is the whole basis of
 * Lambertian diffuse shading: a surface's brightness falls off as the cosine
 * of the angle between its normal and the light direction, because a beam of
 * fixed cross-section spreads over a larger area as the surface tilts away.
 */

inline float dot_Vec3(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

/**
 * @brief Scale v to unit length. Returns the zero vector unchanged.
 *
 * Parameter is BY VALUE, unlike the others: the function mutates its argument
 * and returns it, so it needs its own copy. Taking const& would force an
 * explicit local anyway.
 *
 * The 1e-8f guard prevents division by a near-zero length, which would produce
 * inf/NaN. A NaN propagates silently through the whole pipeline and typically
 * shows up as a missing triangle rather than a crash. Degenerate (zero-area) triangles
 * generate exactly this case.
 */
inline Vec3 normalize_Vec3(Vec3 v) {
    float length = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (length > 1e-8f) {
        v.x /= length;
        v.y /= length;
        v.z /= length;
    }
    return v;
}


/** @brief Debug print: (x, y, z). Uses printf rather than std::cout because
 *  <iostream> is a very large header and injects a static initialiser into
 *  every translation unit that includes it -- and this header is included by
 *  everything. */
inline void print_vector_Vec3(const Vec3& v) {
   std::printf("(%f, %f, %f)\n", v.x, v.y, v.z);
}

/** @brief Debug print: (x, y, z, w). See print_vector_Vec3 for the printf note. */
inline void print_vector_Vec4(const Vec4& v) {
    std::printf("(%f, %f, %f, %f)\n", v.x, v.y, v.z, v.w);
}

