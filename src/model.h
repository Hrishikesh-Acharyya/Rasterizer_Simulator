#pragma once

/**
 * @file model.h
 * @brief Geometry loading: OBJ input, material resolution, bounding-box
 *        measurement.
 *
 * This is the boundary between external file data and the renderer's internal
 * representation. Everything past this point sees uniform, fully populated
 * arrays with every index guaranteed in range; the format's quirks -- 1-based
 * and negative indices, optional attributes, arbitrary polygon sizes, materials
 * defined in a separate file and bound by line order -- all stop here.
 *
 * Includes types.h for RGB, so this header sits above types.h in the include
 * DAG. types.h itself still depends on nothing.
 */

#include "vectors.h"
#include "types.h"
#include <vector>
#include <string>

/**
 * @brief Axis-aligned bounding box in the space of the vertices measured.
 *
 * Produced by normalizationPass() and consumed by the caller to build the model
 * normalisation matrix. Model space, float, one per model -- unrelated to the
 * screen-space integer bounds the rasterizer walks per triangle.
 *
 * Held as two Vec3 rather than six floats so the pairing is enforced by the
 * type: min.x and max.x are different names, not indices 0 and 3, and a swap is
 * a compile error rather than a wrong render. Vec3 arithmetic applies directly,
 * so the centre is (min + max) * 0.5 and the extent is max - min.
 */
struct boundingBox {
    Vec3 min;   ///< Componentwise minimum over every vertex.
    Vec3 max;   ///< Componentwise maximum over every vertex.
};

/**
 * @brief Load an OBJ file into vertices, a flat triangle index list, and a
 *        per-triangle material assignment.
 *
 * @param path  Path to the .obj file, resolved against the current working
 *              directory when relative.
 * @param[out] out_verts          Cleared, then one Vec4 per 'v' line, w = 1
 *                                (positions, not directions).
 * @param[out] out_indices        Cleared, then 3 indices per triangle,
 *                                zero-based, guaranteed within out_verts.
 * @param[out] out_tri_materials  Cleared, then ONE index per triangle into
 *                                out_materials.
 * @param[out] out_materials      Cleared, then the material palette. Entry 0 is
 *                                always a neutral default, so this is never
 *                                empty and every index in out_tri_materials is
 *                                valid.
 * @return false if the .obj could not be opened; true otherwise. A missing or
 *         unreadable .mtl is NOT a failure -- every face falls back to the
 *         default material.
 *
 * @post out_tri_materials.size() * 3 == out_indices.size()
 *
 * Reads 'v', 'f', 'mtllib' and 'usemtl'. Normals ('vn'), texture coordinates
 * ('vt'), smoothing and grouping are skipped, so the caller must synthesise its
 * own normals.
 *
 * Face indices may be 1-based positive or negative (counting backwards from the
 * most recently defined vertex). Both are resolved here. An index of 0, a
 * non-numeric token, or one resolving out of range discards the entire face
 * rather than the offending vertex: dropping one vertex silently reshapes the
 * polygon, and a quad-turned-triangle fans into geometry that looks deliberate,
 * where a missing face is a visible hole.
 *
 * Polygons larger than three vertices are fan-triangulated and assumed CONVEX.
 * A concave face produces triangles outside its own outline.
 *
 * Materials are bound by LINE ORDER, not by naming faces: 'usemtl' selects the
 * material in force for every subsequent 'f' line until the next 'usemtl'. The
 * palette accumulates only materials actually referenced, in first-use order, so
 * a fifty-material .mtl used three times yields four entries including the
 * default. A 'usemtl' naming a material the .mtl did not declare falls back to
 * entry 0 and prints a note.
 *
 * @warning Malformed 'v' lines are not diagnosed. A truncated coordinate leaves
 *          the corresponding component uninitialised.
 */
bool loadOBJ(const std::string& path,
             std::vector<Vec4>& out_verts,
             std::vector<int>&  out_indices,
             std::vector<int>&  out_tri_materials,
             std::vector<RGB>&  out_materials);

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