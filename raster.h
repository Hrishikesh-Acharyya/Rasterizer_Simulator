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
 * @brief Rasterize one triangle into the global framebuffer and zbuffer.
 *
 * Samples at pixel centres. Winding-agnostic: accepts either winding order, so
 * back-facing triangles are drawn and culling must happen upstream.
 *
 * Expects x, y in screen coordinates and z as post-divide NDC depth.
 */
void drawTriangle(const screenVertex& A, const screenVertex& B, const screenVertex& C);