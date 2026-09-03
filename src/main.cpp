/**
 * @file main.cpp
 * @brief Scene setup, the per-frame render loop, and rasterizer selection.
 *
 * Pipeline per frame:
 *   clear buffers -> build MVP -> transform every vertex -> shade per vertex
 *   -> rasterize every triangle -> write PPM
 *
 * The vertex loop runs once per vertex, the triangle loop once per triangle,
 * and the rasterizer's inner loop once per pixel of every surviving triangle's
 * bounding box. On Iron Man at 1080p, PER FRAME, that is 129,759 vertices, then
 * 217,038 triangles, then 5,934,125 bounding-box pixels tested, of which
 * 734,421 are actually covered -- a 12.4% hit rate. Those last two are measured,
 * not estimated: they are the SIG_EDGE and SIG_BARY histogram totals divided by
 * three tallies each and by the 120 frames of the run.
 *
 * That widening ratio is the shape of the whole problem: work per stage grows by
 * orders of magnitude downstream, which is why the fixed-function stages sit at
 * the wide end. It is also why the tally counters are uint64_t -- three edge
 * tallies per bounding-box pixel over 120 frames is 2.14 billion on that signal
 * alone.
 *
 * ## Command line
 *
 *     renderer            float golden reference, output to frames/
 *     renderer <s>        fixed-point coverage at s sub-pixel bits, to frames_s<s>/
 *
 * The selection is a runtime parameter rather than a compile-time one for two
 * reasons. The sweep needs many values of s from one binary, and -- more
 * importantly -- drawTriangle must be provably byte-identical between a golden
 * run and a sweep run. A branch inside it would perturb float codegen, and -O0
 * versus -O2 already moves ~119,000 pixels per frame on the solids scene at
 * 1080p. See raster.cpp for the measurement.
 */

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <vector>

#include "vectors.h"
#include "matrices.h"
#include "types.h"
#include "framebuffer.h"
#include "raster.h"
#include "raster_fixed.h"
#include "model.h"
#include "stats.h"

/**
 * @brief Sub-pixel fractional bits for the fixed-point coverage path.
 *
 * -1 selects the float golden reference. >= 0 snaps vertices to a 2^-s pixel
 * grid and dispatches to drawTriangleFixed.
 *
 * Read by the vertex loop, which does the snapping, and by the triangle loop,
 * which dispatches. Set once from argv and never changed thereafter.
 */
int g_frac_bits = -1;

constexpr float FOV         = 60.0f;   ///< Vertical field of view, DEGREES.
constexpr int   FRAME_COUNT = 120;     ///< 3 degrees per frame, so 120 is one full turn.
constexpr float NEAR_PLANE  = 0.1f;
constexpr float FAR_PLANE   = 100.0f;

/// Path is a named constant so the startup banner can echo it.
static const char* MODEL_PATH = "Media/Obj_files/IronMan.obj";

/// Prefix for the histogram CSVs, WITHOUT extension. Must match MODEL_PATH and
/// the viewport, or the data is filed under the wrong configuration.
///
/// [[maybe_unused]] because STATS_DUMP expands to an unevaluated sizeof when
/// HISTOGRAM_STATS is 0, so this is odr-used but never emitted. GCC says
/// nothing; clang raises -Wunneeded-internal-declaration, which is an error
/// under the -Werror build this README documents.
[[maybe_unused]] static const char* STATS_PREFIX = "stats/IronMan_1080p";

int main(int argc, char** argv)
{
    // ---- Configuration -----------------------------------------------------
    //
    // Parsed before anything expensive happens, so a bad argument fails in
    // milliseconds rather than after a full load.

    char frame_dir[64] = "frames";

    if (argc > 1) {
        // atoi returns 0 on unparseable input rather than reporting an error,
        // and 0 is a valid configuration -- which is why the banner below
        // echoes the parsed value. A typo shows up as "frac_bits = 0".
        g_frac_bits = std::atoi(argv[1]);

        // The upper bound is not arbitrary. The edge accumulator needs
        // W = 19 + 2s bits, so s = 16 gives 51 and fits int64_t with room; at
        // s = 22 it would be 63 and one bit from silent overflow.
        if (g_frac_bits < 0 || g_frac_bits > 16) {
            std::fprintf(stderr, "frac_bits must be 0..16, got %d\n", g_frac_bits);
            return 1;
        }
        std::snprintf(frame_dir, sizeof(frame_dir), "frames_s%d", g_frac_bits);
    }

    // Created here rather than left to `make run`, which only ever makes
    // "frames/". Every frames_s<N>/ a sweep needs was previously the caller's
    // job to mkdir by hand; skip it and every write below fails open() and the
    // run "succeeds" having written nothing. create_directories is also the
    // no-op case when the directory already exists, so this is safe to run
    // unconditionally on every invocation.
    //
    // Non-throwing overload deliberately: the default one throws
    // filesystem_error, and an uncaught exception here would trade a silent
    // failure for an unhandled-exception crash -- worse, not better. A path
    // that exists as a plain file (not a directory) is the case that
    // provokes it; report that plainly and exit instead.
    std::error_code ec;
    std::filesystem::create_directories(frame_dir, ec);
    if (ec) {
        std::fprintf(stderr, "Error: could not create %s/ (%s)\n",
                     frame_dir, ec.message().c_str());
        return 1;
    }

    // Echo the full configuration. Model, resolution and frame count are all
    // compile-time constants, so nothing else distinguishes one output
    // directory from another -- and comparing directories rendered from
    // different configurations produces numbers that look plausible and mean
    // nothing. (Diagnostic: a mean channel difference above ~10 in ppmdiff is
    // this failure, not large quantisation error.)
    std::printf("model=%s  %dx%d  %d frames  frac_bits=%d  output=%s/\n",
                MODEL_PATH, VIEWPORT_WIDTH, VIEWPORT_HEIGHT,
                FRAME_COUNT, g_frac_bits, frame_dir);

    // ---- Load geometry -----------------------------------------------------

    std::vector<Vec4> obj_verts;
    std::vector<int>  obj_indices;
    std::vector<int>  tri_materials;   // one index per TRIANGLE
    std::vector<RGB>  materials;       // palette; entry 0 is the default

    if (!loadOBJ(MODEL_PATH, obj_verts, obj_indices, tri_materials, materials))
        return 1;

    // An n-gon fans into n-2 triangles and each one carries its face's
    // material, so this is the check that the loader pushed the material index
    // inside the triangulation loop rather than once per face. Getting that
    // wrong mis-colours everything after the first quad and reads as a
    // rendering bug rather than a parsing one.
    assert(tri_materials.size() * 3 == obj_indices.size());

    std::vector<screenVertex> screen_verts(obj_verts.size());
    std::vector<Vec3> vertex_normals(obj_verts.size(), {0.0f, 0.0f, 0.0f});
    std::vector<Vec3> world_normals(obj_verts.size());

    const boundingBox box = normalizationPass(obj_verts);

    // ---- Smooth vertex normals ---------------------------------------------
    //
    // For each triangle, compute its face normal and add it to all three of its
    // vertices. A vertex shared by N faces accumulates N contributions.
    //
    // The face normals are added UNNORMALISED, so each contributes in
    // proportion to twice its triangle's area (that is what the cross product's
    // magnitude carries). Larger faces should influence a shared vertex more.
    // Normalising inside this loop would weight every face equally and lose it.
    //
    // This is also why hard and soft edges are a property of the MESH rather
    // than of a renderer flag: a vertex duplicated per face has only its own
    // face to average.

    for (size_t i = 0; i < obj_indices.size(); i += 3) {
        const Vec3 A = {obj_verts[obj_indices[i]].x,
                        obj_verts[obj_indices[i]].y,
                        obj_verts[obj_indices[i]].z};
        const Vec3 B = {obj_verts[obj_indices[i + 1]].x,
                        obj_verts[obj_indices[i + 1]].y,
                        obj_verts[obj_indices[i + 1]].z};
        const Vec3 C = {obj_verts[obj_indices[i + 2]].x,
                        obj_verts[obj_indices[i + 2]].y,
                        obj_verts[obj_indices[i + 2]].z};

        const Vec3 edge1      = subtract_Vec3(B, A);
        const Vec3 edge2      = subtract_Vec3(C, A);
        const Vec3 faceNormal = cross_Vec3(edge1, edge2);

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
    for (Vec3& normal : vertex_normals)
        normal = normalize_Vec3(normal);

    // ---- Model normalisation parameters ------------------------------------
    //
    // Centre the model on the origin and scale its largest axis to span 2
    // units, so any model lands in a predictable place regardless of its
    // authored units. The 2 is not arbitrary: NDC spans [-1, 1], so a model of
    // extent 2 centred at the origin fills the frame before the camera
    // transform pushes it back.
    //
    // Applied via the model matrix, never baked into the vertex data.

    const float centre_x = (box.min.x + box.max.x) / 2.0f;
    const float centre_y = (box.min.y + box.max.y) / 2.0f;
    const float centre_z = (box.min.z + box.max.z) / 2.0f;

    const float extent = std::max({box.max.x - box.min.x,
                                   box.max.y - box.min.y,
                                   box.max.z - box.min.z});

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
    const float s = 2.0f / extent;
    buildScalingMatrix(scaleM, s, s, s);

    // multiply(scale, centre): centre acts FIRST, then scale. Scaling before
    // centring would scale the offset too and displace the model.
    const Mat4 normalise = multiply_matrices(scaleM, centreM);

    // View matrix: pushes the world 2 units down -Z, i.e. the camera sits at
    // +2 looking toward the origin. A translation, since the camera neither
    // rotates nor moves.
    //
    // The distance matters to the bit-width study and is measurable from its
    // own output: NDC depth occupies a single binade, [0.5, 1), which inverts
    // to an eye distance of at least 0.399 and therefore a model bounding
    // radius of at most 1.601. Every histogram and every sweep in the study was
    // taken at this value.
    const Mat4 view = { {{1,0,0,0}, {0,1,0,0}, {0,0,1,-2}, {0,0,0,1}} };

    // Points FROM the surface TOWARD the light, so N.L is directly the cosine
    // falloff with no sign flip.
    const Vec3 lightDir = normalize_Vec3({1.0f, 1.0f, 1.0f});

    // ---- Render loop -------------------------------------------------------

    for (int frame = 0; frame < FRAME_COUNT; ++frame) {

        clear_frameBuffer();
        clear_zBuffer();   // must precede rendering, or last frame's depths
                           // reject this frame's fragments

        const float angle = frame * (PI / 60.0f);   // 3 degrees per frame

        Mat4 rotationxMatrix, rotationyMatrix, rotationzMatrix;
        buildRotationMatrix_x(rotationxMatrix, angle);
        buildRotationMatrix_y(rotationyMatrix, angle);
        buildRotationMatrix_z(rotationzMatrix, angle);
        const Mat4 rotation = multiply_matrices(
            rotationzMatrix, multiply_matrices(rotationyMatrix, rotationxMatrix));

        const Mat4 world_space = multiply_matrices(rotation, normalise);

        // The full chain collapsed into one matrix. Composing once per frame
        // rather than per vertex turns three transforms per vertex into one --
        // the reason a vertex stage takes a single matrix as a uniform.
        const Mat4 mvp = multiply_matrices(perspectiveMatrix,
                             multiply_matrices(view, world_space));

        // ---- Vertex stage --------------------------------------------------

        for (size_t i = 0; i < obj_verts.size(); ++i) {

            // Normals go through world_space with w = 0: directions must not be
            // translated. Renormalised afterwards to undo the uniform model
            // scale, which changes a normal's length but not its direction.
            const Vec4 vertex_normal_augmented = {vertex_normals[i].x,
                                                  vertex_normals[i].y,
                                                  vertex_normals[i].z, 0};
            const Vec4 n = transform_Vec4(world_space, vertex_normal_augmented);
            world_normals[i] = normalize_Vec3({n.x, n.y, n.z});

            // Positions go through the full MVP, which already includes
            // world_space -- transforming a world-space vertex here would apply
            // the model matrix twice.
            const Vec4 ndc = perspectiveTransform(obj_verts[i], mvp);

            // Viewport transform: NDC [-1,1] -> pixel coordinates. Y is flipped
            // because NDC has +Y up while framebuffer row 0 is the top of the
            // screen. Kept in floats -- rounding to integers here would quantise
            // geometry and cost sub-pixel accuracy.
            //
            // Colour is deliberately not set here. It is per-TRIANGLE state,
            // not per-vertex data, so the triangle loop below fills it. The
            // snapped xi/yi are explicitly zeroed rather than left out, so the
            // initialiser stays complete under -Wmissing-field-initializers.
            screen_verts[i] = { (ndc.x + 1.0f) * 0.5f * VIEWPORT_WIDTH,
                                (1.0f - (ndc.y + 1.0f) * 0.5f) * VIEWPORT_HEIGHT,
                                ndc.z,
                                ndc.w,
                                0, 0,
                                {0, 0, 0} };

            // Snap ONCE PER VERTEX, here rather than inside drawTriangleFixed.
            // Two triangles sharing edge AB then read bit-identical integers
            // and compute a bit-identical edge function, so the shared edge is
            // in exactly the same place for both and no crack opens between
            // them. Snapping per triangle would give the same answer in
            // practice but would not guarantee it.
            //
            // lrintf rounds to NEAREST. A plain cast truncates toward zero,
            // which is a systematic -0.5 LSB bias rather than noise, and a bias
            // shifts every triangle edge the same way.
            if (g_frac_bits >= 0) {
                const float snap_scale = (float)(1 << g_frac_bits);
                screen_verts[i].xi = (std::int32_t)lrintf(screen_verts[i].x * snap_scale);
                screen_verts[i].yi = (std::int32_t)lrintf(screen_verts[i].y * snap_scale);
            }

            TALLY(SIG_SCREEN, screen_verts[i].x);
            TALLY(SIG_SCREEN, screen_verts[i].y);
            TALLY(SIG_RECW,   screen_verts[i].rec_w);
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

            // Base colour is per-triangle state selected from the palette, not
            // per-vertex data: one usemtl block can cover thousands of
            // triangles. i steps by 3 through obj_indices, so i/3 is the
            // triangle number.
            const RGB base = materials[tri_materials[i / 3]];

            // Lambertian diffuse with a constant ambient term. For two unit
            // vectors the dot product is exactly cos(theta), which is the whole
            // basis of the model: brightness falls off as the cosine of the
            // angle between normal and light, because a beam of fixed
            // cross-section spreads over a larger area as the surface tilts.
            // max(0, N.L) clamps surfaces facing away rather than letting them
            // go negative; the 0.4 ambient keeps them visible rather than black.
            const float intensityA = 0.4f + 0.6f * std::max(0.0f, dot_Vec3(world_normals[ia], lightDir));
            const float intensityB = 0.4f + 0.6f * std::max(0.0f, dot_Vec3(world_normals[ib], lightDir));
            const float intensityC = 0.4f + 0.6f * std::max(0.0f, dot_Vec3(world_normals[ic], lightDir));

            // Base colour times intensity, evaluated per vertex; the rasterizer
            // interpolates the products. base is identical at all three
            // vertices and only the intensity differs, so the three colours
            // share a hue and vary only in brightness.
            //
            // Intensity is at most 1.0 here, so these casts cannot overflow.
            // They TRUNCATE rather than round, biasing every channel down by up
            // to one level, and a specular term pushing intensity above 1 would
            // wrap a bright highlight to black.
            //
            // How close these three colours are is what sets the irreducible
            // one-level noise floor between the float and fixed paths: on a
            // 310-pixel triangle they differ enough that a last-bit weight
            // error crosses a level, while on a 7-pixel triangle they are
            // nearly equal and the interpolated value barely depends on the
            // weights at all. Dense tessellation suppresses the floor.
            A.color = { std::uint8_t(base.r * intensityA),
                        std::uint8_t(base.g * intensityA),
                        std::uint8_t(base.b * intensityA) };

            B.color = { std::uint8_t(base.r * intensityB),
                        std::uint8_t(base.g * intensityB),
                        std::uint8_t(base.b * intensityB) };

            C.color = { std::uint8_t(base.r * intensityC),
                        std::uint8_t(base.g * intensityC),
                        std::uint8_t(base.b * intensityC) };

            // One predictable branch per triangle -- thousands per frame rather
            // than millions, and on a global that never changes mid-run.
            if (g_frac_bits < 0)
                drawTriangle(A, B, C);
            else
                drawTriangleFixed(A, B, C, g_frac_bits);
        }

        // Zero-padded so lexical order matches temporal order, which is what
        // ffmpeg's sequence globbing and ppmdiff's frame numbering expect.
        char filename[256];
        std::snprintf(filename, sizeof(filename), "%s/%03d.ppm", frame_dir, frame);

        // Fatal rather than logged-and-continued: a run that cannot write its
        // frames must not return 0. create_directories above prevents the
        // common cause, but a write can still fail on a full disk or a
        // permissions problem, and this is the difference between that
        // surfacing immediately and a sweep silently comparing against
        // whatever frames happen to already be on disk.
        if (!writeFramebufferToPPM(filename)) {
            std::fprintf(stderr, "Aborting: frame %d could not be written.\n", frame);
            return 1;
        }
    }

    STATS_DUMP(STATS_PREFIX);

    return 0;
}