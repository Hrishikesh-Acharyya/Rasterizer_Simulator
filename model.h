#pragma once

/**
 * @file model.h
 * @brief Geometry loading: OBJ file input and bounding-box measurement.
 *
 * This is the boundary between external file data and the renderer's internal
 * representation. Everything past this point should see uniform, fully
 * populated arrays; the format's quirks (1-based indices, optional attributes,
 * arbitrary polygon sizes) stop here.
 */

#include "vectors.h"
#include <vector>
#include <string>

/**
 * @brief Load an OBJ file into a vertex array and a flat triangle index list.
 *
 * @param path         Path to the .obj file, resolved against the current
 *                     working directory when relative.
 * @param[out] out_verts    Cleared, then filled with one Vec4 per 'v' line,
 *                          w = 1 (positions, not directions).
 * @param[out] out_indices  Cleared, then filled with 3 indices per triangle,
 *                          zero-based, indexing into out_verts.
 * @return false if the file could not be opened; true otherwise.
 *
 * Only 'v' and 'f' lines are read. Normals, texture coordinates, materials and
 * grouping are ignored, so a caller must synthesise normals itself.
 *
 * Polygons with more than three vertices are triangulated. Faces are assumed
 * convex; a concave polygon produces triangles outside its own outline.
 *
 * Malformed lines are not diagnosed. A truncated or non-OBJ file may return
 * true with empty or partial output.
 */
bool loadOBJ(const std::string& path,
             std::vector<Vec4>& out_verts,
             std::vector<int>&  out_indices);

/**
 * @brief Axis-aligned bounding box of a vertex array.
 *
 * @param obj_verts  Vertices to measure. w is ignored.
 * @return Six floats in the order {min_x, min_y, min_z, max_x, max_y, max_z}.
 *
 * The caller uses these to centre and scale the model, so index order is part
 * of the contract: index 3 is max_x, not min_w.
 *
 * That ordering is the interface's weakness — a fixed-size result returned as a
 * heap-allocated vector, addressed by number, where a wrong index yields a
 * wrong render rather than an error. A small named struct is the right shape
 * and is a planned change.
 *
 * An empty input returns the sentinel values the scan starts from (min = FLT_MAX,
 * max = FLT_LOWEST), which produce a negative extent downstream.
 */
std::vector<float> normalizationPass(const std::vector<Vec4>& obj_verts);