/**
 * @file main.cpp
 * @brief Scene setup and the per-frame render loop.
 *
 * Pipeline per frame:
 *   clear buffers -> build MVP -> transform every vertex -> shade per vertex
 *   -> rasterize every triangle -> write PPM
 *
 * The vertex loop runs once per vertex (~800), the triangle loop once per
 * triangle (~1600), and the rasterizer's inner loop runs once per pixel of
 * coverage (millions). That widening ratio is the shape of the whole problem:
 * work per stage grows by orders of magnitude downstream, which is why the
 * fixed-function stages sit at the wide end.
 */

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <vector>

#include "vectors.h"
#include "matrices.h"
#include "types.h"
#include "framebuffer.h"
#include "raster.h"
#include "model.h"

// extern int g_tris_culled;
// extern int g_tris_drawn;

constexpr float FOV = 60.0f;          // vertical field of view, DEGREES
constexpr int   FRAME_COUNT = 120;
constexpr float NEAR_PLANE = 0.1f;
constexpr float FAR_PLANE  = 100.0f;

int main() {

    // ---- Load geometry -----------------------------------------------------

    std::vector<Vec4> obj_verts;
    std::vector<int>  obj_indices;
    if (!loadOBJ("Media/Obj_files/torus.obj", obj_verts, obj_indices)) {
        return 1;
    }

    std::vector<screenVertex> screen_verts(obj_verts.size());
    std::vector<Vec3> vertex_normals(obj_verts.size(), {0.0f, 0.0f, 0.0f});
    std::vector<Vec3> world_normals(obj_verts.size());

    boundingBox box = normalizationPass(obj_verts);

    // ---- Smooth vertex normals ---------------------------------------------
    //
    // For each triangle, compute its face normal and add it to all three of its
    // vertices. A vertex shared by N faces accumulates N contributions.
    //
    // The face normals are added UNNORMALISED, so each contributes in
    // proportion to twice its triangle's area (that is what the cross product's
    // magnitude carries). Larger faces should influence a shared vertex more.
    // Normalising inside this loop would weight every face equally and lose it.

    for (size_t i = 0; i < obj_indices.size(); i += 3) {
        Vec3 A = {obj_verts[obj_indices[i]].x,     obj_verts[obj_indices[i]].y,     obj_verts[obj_indices[i]].z};
        Vec3 B = {obj_verts[obj_indices[i + 1]].x, obj_verts[obj_indices[i + 1]].y, obj_verts[obj_indices[i + 1]].z};
        Vec3 C = {obj_verts[obj_indices[i + 2]].x, obj_verts[obj_indices[i + 2]].y, obj_verts[obj_indices[i + 2]].z};

        Vec3 edge1 = subtract_Vec3(B, A);
        Vec3 edge2 = subtract_Vec3(C, A);
        Vec3 faceNormal = cross_Vec3(edge1, edge2);

        vertex_normals[obj_indices[i]].x     += faceNormal.x;
        vertex_normals[obj_indices[i]].y     += faceNormal.y;
        vertex_normals[obj_indices[i]].z     += faceNormal.z;

        vertex_normals[obj_indices[i + 1]].x += faceNormal.x;
        vertex_normals[obj_indices[i + 1]].y += faceNormal.y;
        vertex_normals[obj_indices[i + 1]].z += faceNormal.z;

        vertex_normals[obj_indices[i + 2]].x += faceNormal.x;
        vertex_normals[obj_indices[i + 2]].y += faceNormal.y;
        vertex_normals[obj_indices[i + 2]].z += faceNormal.z;
    }

    // Normalise only after every contribution has been summed.
    for (Vec3& normal : vertex_normals) {
        normal = normalize_Vec3(normal);
    }

   // ---- Model normalisation parameters ------------------------------------
//
// Centre the model on the origin and scale its largest axis to span 2 units,
// so any model lands in a predictable place regardless of its authored units.
// The 2 is not arbitrary: NDC spans [-1, 1], so a model of extent 2 centred at
// the origin fills the frame before the camera transform pushes it back.
//
// Applied via the model matrix, never baked into the vertex data.
    float centre_x = (box.min.x + box.max.x) / 2.0f;
    float centre_y = (box.min.y + box.max.y) / 2.0f;
    float centre_z = (box.min.z + box.max.z) / 2.0f;

    float extent = std::max({box.max.x-box.min.x,
                             box.max.y-box.min.y,
                             box.max.z-box.min.z});

    // ---- Base vertex colours -----------------------------------------------
    //
    // PLACEHOLDER. Maps each vertex's position within the bounding box onto RGB
    // so the geometry is legible without materials. Not a real OBJ concept --
    // colour comes from a material or a texture. To be replaced by MTL Kd.

    std::vector<RGB> obj_colors(obj_verts.size());
    {
        float min_x = box.min.x, max_x = box.max.x;
        float min_y = box.min.y, max_y = box.max.y;
        float min_z = box.min.z, max_z = box.max.z;

        float range_x = max_x - min_x;
        float range_y = max_y - min_y;
        float range_z = max_z - min_z;

        for (size_t i = 0; i < obj_verts.size(); ++i) {
            // Fall back to 0.5 when the model is flat on an axis, avoiding a
            // divide by zero.
            float fx = (range_x > 1e-8f) ? (obj_verts[i].x - min_x) / range_x : 0.5f;
            float fy = (range_y > 1e-8f) ? (obj_verts[i].y - min_y) / range_y : 0.5f;
            float fz = (range_z > 1e-8f) ? (obj_verts[i].z - min_z) / range_z : 0.5f;

            obj_colors[i] = { std::uint8_t(fx * 255.0f),
                              std::uint8_t(fy * 255.0f),
                              std::uint8_t(fz * 255.0f) };
        }
    }

    // ---- Static matrices ---------------------------------------------------
    //
    // Built once outside the loop: nothing about the projection, the camera or
    // the model normalisation changes between frames.

    Mat4 perspectiveMatrix;
    buildPerspectiveMatrix(perspectiveMatrix,
                           FOV * (PI / 180.0f),      // builder expects radians
                           float(VIEWPORT_WIDTH) / float(VIEWPORT_HEIGHT),
                           NEAR_PLANE, FAR_PLANE);

    Mat4 centreM, scaleM;
    buildTranslationMatrix(centreM, -centre_x, -centre_y, -centre_z);
    float s = 2.0f / extent;
    buildScalingMatrix(scaleM, s, s, s);

    // multiply(scale, centre): centre acts FIRST, then scale. Scaling before
    // centring would scale the offset too and displace the model.
    Mat4 normalise = multiply_matrices(scaleM, centreM);

    // View matrix: pushes the world 4 units down -Z, i.e. the camera sits at
    // +4 looking toward the origin. A translation, since the camera neither
    // rotates nor moves.
    Mat4 view = { {{1,0,0,0},{0,1,0,0},{0,0,1,-4},{0,0,0,1}} };

    // Points FROM the surface TOWARD the light, so N.L is directly the cosine
    // falloff with no sign flip.
    Vec3 lightDir = normalize_Vec3({1.0f, 1.0f, 1.0f});

    // ---- Render loop -------------------------------------------------------

    for (int frame = 0; frame < FRAME_COUNT; ++frame) {

//         if (frame == 1) {
//     std::printf("culled %d, drawn %d (%.1f%%)\n",
//                 g_tris_culled, g_tris_drawn,
//                 100.0f * g_tris_culled / (g_tris_culled + g_tris_drawn));
// }
        clear_frameBuffer();
        clear_zBuffer();   // must precede rendering, or last frame's depths
                           // reject this frame's fragments

        float angle = frame * (PI / 60.0f);   // 3 degrees per frame

        Mat4 rotationxMatrix, rotationyMatrix, rotationzMatrix;
        buildRotationMatrix_x(rotationxMatrix, angle);
        buildRotationMatrix_y(rotationyMatrix, angle);
        buildRotationMatrix_z(rotationzMatrix, angle);
        Mat4 rotation = multiply_matrices(rotationzMatrix,
                            multiply_matrices(rotationyMatrix, rotationxMatrix));

        Mat4 world_space = multiply_matrices(rotation, normalise);

        // The full chain collapsed into one matrix. Composing once per frame
        // rather than per vertex turns three transforms per vertex into one --
        // the reason a vertex stage takes a single matrix as a uniform.
        Mat4 mvp = multiply_matrices(perspectiveMatrix,
                       multiply_matrices(view, world_space));

        // ---- Vertex stage --------------------------------------------------

        for (size_t i = 0; i < obj_verts.size(); ++i) {

            // Normals go through world_space with w = 0: directions must not be
            // translated. Renormalised afterwards to undo the uniform model
            // scale, which changes a normal's length but not its direction.
            Vec4 vertex_normal_augmented = {vertex_normals[i].x,
                                            vertex_normals[i].y,
                                            vertex_normals[i].z, 0};
            Vec4 n = transform_Vec4(world_space, vertex_normal_augmented);
            world_normals[i] = normalize_Vec3({n.x, n.y, n.z});

            // Positions go through the full MVP, which already includes
            // world_space -- transforming a world-space vertex here would apply
            // the model matrix twice.
            Vec3 transformed = perspectiveTransform(obj_verts[i], mvp);

            // Viewport transform: NDC [-1,1] -> pixel coordinates. Y is flipped
            // because NDC has +Y up while framebuffer row 0 is the top of the
            // screen. Kept in floats -- rounding to integers here would quantise
            // geometry and cost sub-pixel accuracy.
            screen_verts[i] = { (transformed.x + 1.0f) * 0.5f * VIEWPORT_WIDTH,
                                (1.0f - (transformed.y + 1.0f) * 0.5f) * VIEWPORT_HEIGHT,
                                transformed.z,
                                obj_colors[i] };
        }

        // ---- Shading and rasterization -------------------------------------
        //
        // Gouraud: lighting is evaluated once per VERTEX here, and the
        // rasterizer interpolates the resulting colours across each face. The
        // alternative (Phong) evaluates per pixel, which is far more accurate
        // on large triangles and far more expensive -- one N.L per fragment
        // instead of one per vertex.

        for (size_t i = 0; i < obj_indices.size(); i += 3) {

            screenVertex A = screen_verts[obj_indices[i]];
            screenVertex B = screen_verts[obj_indices[i + 1]];
            screenVertex C = screen_verts[obj_indices[i + 2]];

            const int ia = obj_indices[i];
            const int ib = obj_indices[i + 1];
            const int ic = obj_indices[i + 2];

            // Lambertian diffuse with a constant ambient term.
            // max(0, N.L) clamps surfaces facing away from the light to zero
            // rather than letting them go negative; the 0.4 ambient keeps them
            // visible rather than black.
            float intensityA = 0.4f + 0.6f * std::max(0.0f, dot_Vec3(world_normals[ia], lightDir));
            float intensityB = 0.4f + 0.6f * std::max(0.0f, dot_Vec3(world_normals[ib], lightDir));
            float intensityC = 0.4f + 0.6f * std::max(0.0f, dot_Vec3(world_normals[ic], lightDir));

            // Intensity is at most 1.0 here, so the uint8_t casts cannot
            // overflow. They TRUNCATE rather than clamp, so adding a specular
            // term or any intensity above 1 would wrap a bright highlight to
            // black.
            A.color = { std::uint8_t(A.color.r * intensityA),
                        std::uint8_t(A.color.g * intensityA),
                        std::uint8_t(A.color.b * intensityA) };

            B.color = { std::uint8_t(B.color.r * intensityB),
                        std::uint8_t(B.color.g * intensityB),
                        std::uint8_t(B.color.b * intensityB) };

            C.color = { std::uint8_t(C.color.r * intensityC),
                        std::uint8_t(C.color.g * intensityC),
                        std::uint8_t(C.color.b * intensityC) };

          
            drawTriangle(A, B, C);
        }

        // Zero-padded so lexical order matches temporal order, which is what
        // ffmpeg's sequence globbing expects.
        char filename[256];
        std::snprintf(filename, sizeof(filename), "frames/%03d.ppm", frame);
        writeFramebufferToPPM(filename);
    }

    return 0;
}