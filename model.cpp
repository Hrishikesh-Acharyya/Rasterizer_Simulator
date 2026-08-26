/**
 * @file model.cpp
 * @brief OBJ parsing and bounding-box measurement.
 *
 * The parser reads the subset of OBJ the renderer currently consumes and skips
 * the rest. Index resolution is complete — 1-based, negative and out-of-range
 * indices are all handled here — because that is the boundary: everything past
 * this point sees zero-based indices guaranteed to be in range.
 */

#include "model.h"
#include <fstream>
#include <sstream>
#include <cstdio>
#include <limits>
#include <algorithm>
#include <stdexcept>

bool loadOBJ(const std::string& path,
             std::vector<Vec4>& out_verts,
             std::vector<int>&  out_indices)
{
    std::ifstream file(path);
    if (!file) {
        std::printf("Error: could not open %s\n", path.c_str());
        return false;
    }

    out_verts.clear();
    out_indices.clear();

    // OBJ is line-oriented: a leading tag names the record type, the rest of
    // the line is its payload. Parsing each line into its own stringstream
    // keeps a malformed line from consuming the next one's tokens.
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream ss(line);
        std::string tag;
        ss >> tag;

        if (tag == "v") {
            // w = 1: these are POSITIONS, so translation must affect them.
            // A 'vn' direction would need w = 0.
            float x, y, z;
            ss >> x >> y >> z;
            out_verts.push_back({x, y, z, 1.0f});
        }
        else if (tag == "f") {
            std::vector<int> face;
            std::string token;

            while (ss >> token) {
                // A face vertex is "v", "v/vt", "v//vn" or "v/vt/vn" — three
                // INDEPENDENT index streams into three separately-sized
                // arrays. The format splits them so one position can carry two
                // normals across a hard edge, or two texcoords across a UV
                // seam.
                //
                // Only the position index is kept, which is why the renderer
                // must synthesise its own normals. Consuming vt and vn means
                // unwelding: every distinct (v, vt, vn) triple becomes one
                // entry in a single vertex array, so the rasterizer sees one
                // index fetching one complete vertex.
                std::size_t slash = token.find('/');
                if (slash != std::string::npos)
                    token = token.substr(0, slash);

                int raw = 0;
                try {
                    raw = std::stoi(token);
                } catch (const std::exception&) {
                    face.clear();
                    break;
                }

                // OBJ indices are 1-based, and may be NEGATIVE: -1 is the most
                // recently defined vertex at this point in the file, -2 the one
                // before it. That is what makes OBJ files concatenable —
                // appending one model to another leaves negative-indexed faces
                // still pointing at their own vertices, where absolute indices
                // would all shift.
                //
                // out_verts.size() is exactly that running count, because the
                // file is parsed in a single forward pass. A loader that read
                // every 'v' line first would need to track the count
                // separately.
                int resolved;
                if (raw > 0) {
                    resolved = raw - 1;
                } else if (raw < 0) {
                    resolved = static_cast<int>(out_verts.size()) + raw;
                } else {
                    face.clear();   // index 0 does not exist in OBJ
                    break;
                }

                if (resolved < 0 || resolved >= static_cast<int>(out_verts.size())) {
                    face.clear();
                    break;
                }

                face.push_back(resolved);
            }

            // Every failure above clears the face and breaks, discarding the
            // whole polygon rather than the offending vertex. Dropping one
            // vertex would silently reshape the face — a quad becomes a
            // triangle and fan-triangulates into geometry that looks
            // deliberate. A missing face is a visible hole; a reshaped one is
            // not.
            //
            // Fan triangulation: (0,1,2), (0,2,3), (0,3,4)... Every triangle
            // shares vertex 0, so an n-gon becomes n-2 triangles. An empty
            // face makes this loop's condition 1 < 0, so it emits nothing and
            // needs no separate guard.
            //
            // Correct only for CONVEX polygons. On a concave face, some fan
            // triangles fall outside the outline. The quads in the test models
            // are convex, so this holds for now.
            for (std::size_t i = 1; i + 1 < face.size(); ++i) {
                out_indices.push_back(face[0]);
                out_indices.push_back(face[i]);
                out_indices.push_back(face[i + 1]);
            }
        }
        // vn, vt, s, g, o, usemtl, mtllib and comments are all skipped.
    }

    // size() returns size_t, which is 8 bytes on 64-bit while %d expects 4.
    // printf is variadic, so the format string is its only type information and
    // a mismatch corrupts every argument after it. Cast narrowly at the call
    // rather than using %zu, so the format string stays portable to toolchains
    // whose runtime predates C99.
    std::printf("Loaded %d vertices and %d triangles\n",
                static_cast<int>(out_verts.size()),
                static_cast<int>(out_indices.size()) / 3);
    return true;
}

/**
 * Single pass, one comparison per axis per vertex, no allocation anywhere —
 * the result is a fixed-size aggregate constructed directly in the caller's
 * storage.
 *
 * min starts at +FLT_MAX and max at FLT_LOWEST, each at the opposite extreme,
 * so the first real vertex replaces both. Seeding from 0 would clamp the box to
 * include the origin whether or not the model does.
 *
 * lowest(), not min(): for floating point, min() is the smallest POSITIVE
 * normal value — a tiny number just above zero, not the most negative float.
 * Seeding max with it leaves max stuck near zero for any model sitting entirely
 * in negative space, producing a box that does not contain its own geometry.
 */
boundingBox normalizationPass(const std::vector<Vec4>& obj_verts)
{
    boundingBox box;
    box.min = { std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max() };
    box.max = { std::numeric_limits<float>::lowest(),
                std::numeric_limits<float>::lowest(),
                std::numeric_limits<float>::lowest() };

    for (std::size_t i = 0; i < obj_verts.size(); ++i) {
        box.min.x = std::min(box.min.x, obj_verts[i].x);
        box.min.y = std::min(box.min.y, obj_verts[i].y);
        box.min.z = std::min(box.min.z, obj_verts[i].z);

        box.max.x = std::max(box.max.x, obj_verts[i].x);
        box.max.y = std::max(box.max.y, obj_verts[i].y);
        box.max.z = std::max(box.max.z, obj_verts[i].z);
    }

    return box;
}