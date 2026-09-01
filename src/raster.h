#pragma once

/**
 * @file raster.h
 * @brief Floating-point triangle rasterization: coverage, depth test, colour.
 *
 * This is the GOLDEN REFERENCE. Every fixed-point configuration is diffed
 * against its output pixel for pixel, so nothing in this translation unit may
 * change without invalidating the comparison. The fixed-point rasterizer lives
 * beside it in raster_fixed.h rather than behind a flag inside it, precisely so
 * that this code path is provably identical between a golden run and a sweep
 * run -- adding a branch here would change instruction selection around it, and
 * float codegen is sensitive enough that -O0 versus -O2 already moves ~135,000
 * pixels per frame at 1080p.
 *
 * Deliberately does not include framebuffer.h, though raster.cpp does. A caller
 * needs screenVertex, not the global buffers.
 */

#include "types.h"

/**
 * @brief Signed area of triangle ABC, i.e. (B-A) x (C-A).
 *
 * @param ax,ay  First vertex of the directed edge.
 * @param bx,by  Second vertex of the directed edge.
 * @param cx,cy  Point being classified against edge AB.
 * @return Twice the signed area. The sign indicates which side of directed edge
 *         AB the point C lies on; zero means exactly on the line.
 *
 * Doubles as the unnormalised barycentric weight of C. Evaluated three times
 * per pixel of every triangle's bounding box, which makes it the highest-traffic
 * arithmetic in the whole program.
 *
 * @note The three edge functions of a triangle satisfy w0 + w1 + w2 == area
 *       identically, for ANY sample point. Inside the triangle all three share
 *       a sign, so none can exceed the total: max|E(P)| == |area|, attained at
 *       the vertex opposite the edge. That identity is what makes the value's
 *       upper bound a deterministic property of triangle size rather than of
 *       where pixels happen to fall.
 */
float edge_function(float ax, float ay, float bx, float by, float cx, float cy);

/**
 * @brief Screen-space bounding box of triangle ABC.
 *
 * @param ax,ay,bx,by,cx,cy  Triangle vertices in screen coordinates.
 * @param[out] min_x,min_y,max_x,max_y  Inclusive pixel bounds, clamped to the
 *             viewport.
 *
 * @warning The clamp is load-bearing. drawTriangle's buffer writes are
 *          unchecked, so out-of-range bounds index past the end of the vector
 *          and corrupt memory rather than raising an error.
 *
 * Out-parameters rather than a returned struct; a BBox return would read better
 * at the call site and cost nothing under RVO.
 */
void bounding_box(float ax, float ay, float bx, float by, float cx, float cy,
                  int& min_x, int& min_y, int& max_x, int& max_y);

/**
 * @brief Rasterize one screen-space triangle into the global framebuffer.
 *
 * @param A,B,C  Screen-space vertices: x,y in pixels, z in NDC, plus colour.
 *
 * Back-facing triangles are discarded before any per-pixel work, as are
 * degenerate ones with zero projected area.
 *
 * @warning Requires CONSISTENT WINDING across the mesh. Facing is decided from
 *          the sign of the signed screen-space area, so a mesh wound the
 *          opposite way is culled in its entirety and renders black. OBJ
 *          specifies counter-clockwise front faces, but exporters are
 *          unreliable and a negative scale reverses every triangle.
 *
 * @see drawTriangleFixed for the fixed-point coverage path.
 */
void drawTriangle(const screenVertex& A, const screenVertex& B, const screenVertex& C);