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
 * @brief Axis-aligned bounding box in the space of the vertices measured.
 *
 * Produced by normalizationPass() and consumed by the caller to build the model
 * normalisation matrix. Model space, float, one per model — unrelated to the
 * screen-space integer bounds the rasterizer walks per triangle.
 *
 * Held as two Vec3 rather than six floats so the pairing is enforced by the
 * type: min.x and max.x are different names, not indices 0 and 3, and a swap
 * is a compile error rather than a wrong render. Vec3 arithmetic applies
 * directly, so the centre is (min + max) * 0.5 and the extent is max - min.
 */

struct boundingBox{

  Vec3 min;
  Vec3 max;
};

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
 * @brief Measure the axis-aligned bounding box of a vertex array.
 *
 * @param obj_verts  Vertices to measure. The w component is ignored.
 * @return The componentwise min and max over all vertices.
 *
 * The caller uses this to centre the model on the origin and scale its largest
 * axis to span 2 units, matching the NDC range the projection targets. That
 * normalisation is applied through the model matrix rather than baked into the
 * vertex data, so the loaded geometry stays exactly as authored.
 *
 * @warning An empty input returns the sentinels the scan starts from
 *          (min = FLT_MAX, max = -FLT_MAX), an inverted box. The extent comes
 *          out negative and the model renders inside out rather than failing.
 *          The caller is responsible for not asking.
 */
boundingBox normalizationPass(const std::vector<Vec4>& obj_verts);