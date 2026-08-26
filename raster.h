#pragma once

/**
 * @file raster.h
 * @brief Triangle rasterization: coverage, depth test, colour interpolation.
 *
 * Deliberately does not include framebuffer.h, though raster.cpp does. A caller
 * needs screenVertex, not the global buffers.
 */

#include "types.h"

/**
 * @brief Signed area of triangle ABC, i.e. (B-A) x (C-A).
 * @return Twice the signed area. Sign indicates which side of directed edge AB
 *         the point C lies on; zero means exactly on the line.
 *
 * Doubles as the unnormalised barycentric weight of C.
 */
float edge_function(float ax, float ay, float bx, float by, float cx, float cy);

/**
 * @brief Screen-space bounding box of triangle ABC.
 * @param ax,ay,bx,by,cx,cy  Triangle vertices in screen coordinates.
 * @param[out] min_x,min_y,max_x,max_y  Inclusive pixel bounds, clamped to the
 *             viewport. The clamp is required: drawTriangle's writes are
 *             unchecked, so out-of-range bounds corrupt memory.
 *
 * Out-parameters rather than a returned struct; a BBox return would read better
 * at the call site and cost nothing under RVO.
 */
void bounding_box(float ax, float ay, float bx, float by, float cx, float cy,
                  int& min_x, int& min_y, int& max_x, int& max_y);

/**
 * @brief Rasterize one screen-space triangle into the framebuffer.
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
 */
void drawTriangle(const screenVertex& A, const screenVertex& B, const screenVertex& C);
