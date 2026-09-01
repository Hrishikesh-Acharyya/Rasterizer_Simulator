/**
 * @file raster.cpp
 * @brief Floating-point rasterizer implementation -- the golden reference.
 *
 * Includes framebuffer.h, which raster.h deliberately does not: the
 * implementation writes to the global buffers, the interface does not need to
 * know they exist.
 *
 * Nothing here may change without invalidating every fixed-point comparison in
 * the study. That is also why the fixed-point path is a separate function in a
 * separate translation unit rather than a branch inside drawTriangle: float
 * codegen is sensitive enough that adding a branch would perturb instruction
 * selection around it, and rebuilding this file at -O0 instead of -O2 already
 * moves ~135,000 pixels per frame at 1080p on the solids scene.
 */

#include "raster.h"

#include "framebuffer.h"
#include "stats.h"
#include <algorithm>   // std::min, std::max, initialiser-list overloads
#include <cmath>       // std::floor, std::ceil, std::fabs
#include <cstdint>     // std::uint8_t

/**
 * Two multiplies and five adds. No division, no branch, no memory access.
 *
 * The whole coverage test is three of these plus three sign comparisons, and
 * the cost is identical for every pixel regardless of geometry. That uniformity
 * is what makes rasterization parallelisable: no pixel's result depends on any
 * other's, so an arbitrary number can be evaluated at once.
 *
 * DEFERRED: E is affine in the sample point, so
 *     E(x+1, y) = E(x, y) - (by - ay)
 *     E(x, y+1) = E(x, y) + (bx - ax)
 * with both increments constant for the whole triangle. Evaluating once at a
 * tile corner and walking with adds turns six multiplies per pixel into two
 * adds. Not done here because the reference is meant to be obvious rather than
 * fast, and because incremental evaluation in FLOAT accumulates rounding --
 * the same walk from two different corners would not agree, which is exactly
 * the crack the fixed-point path exists to prevent. It belongs in the RTL.
 */
float edge_function(float ax, float ay, float bx, float by, float cx, float cy)
{
    return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
}

void bounding_box(float ax, float ay, float bx, float by, float cx, float cy,
                  int& min_x, int& min_y, int& max_x, int& max_y)
{
    // min/max on the FLOATS first, then floor/ceil, then cast. Reversing this
    // rounds each vertex to the integer grid before comparing, which can drop a
    // pixel of coverage at the edges.
    min_x = static_cast<int>(std::floor(std::min({ax, bx, cx})));
    max_x = static_cast<int>(std::ceil (std::max({ax, bx, cx})));
    min_y = static_cast<int>(std::floor(std::min({ay, by, cy})));
    max_y = static_cast<int>(std::ceil (std::max({ay, by, cy})));

    // Clamp to the viewport. drawTriangle's buffer writes are unchecked, so an
    // off-screen vertex without this clamp indexes past the end of the vector
    // and corrupts memory rather than raising an error.
    min_x = std::max(min_x, 0);
    min_y = std::max(min_y, 0);
    max_x = std::min(max_x, VIEWPORT_WIDTH  - 1);
    max_y = std::min(max_y, VIEWPORT_HEIGHT - 1);
}

/**
 * Cost per stage, and why the ordering is what it is.
 *
 * Per pixel in the bounding box: 3 edge functions and one depth-buffer READ.
 * Per covered pixel: 3 divisions to normalise the weights, plus the depth
 * interpolation. Per pixel that also PASSES the depth test: one reciprocal for
 * the perspective correction, 3 multiplies, 3 colour interpolations, a depth
 * WRITE and a colour WRITE.
 *
 * The read happens whether or not the pixel is covered, and consecutive pixels
 * share no data, so this is streaming traffic with no reuse. At 1080p that is
 * millions of accesses per frame, and it dominates the bandwidth budget long
 * before the arithmetic does.
 *
 * The ordering is deliberate: work moves below the depth test wherever it can,
 * so an occluded fragment pays for coverage and depth and nothing else. On a
 * self-occluding mesh that is a large fraction of covered fragments -- measured
 * overdraw is 1.35 on the solids scene and 4.48 on Iron Man.
 *
 * Measured fill rate is 28.3% on the solids scene and 12.4% on Iron Man, i.e.
 * up to 88% of edge evaluations land on pixels outside the triangle. That is
 * the argument for hierarchical or edge-walking traversal over a naive
 * bounding-box scan.
 *
 * DEFERRED: `area` is constant across the whole triangle, so 1.0f/area could be
 * hoisted out of the loop and the three divisions become three multiplies. Not
 * done here because reciprocal-multiply and division round differently in
 * floating point, so it changes output bit-for-bit and belongs in its own
 * commit with its own verification.
 */
void drawTriangle(const screenVertex& A, const screenVertex& B, const screenVertex& C)
{
    // Signed area of the whole triangle: the facing test below, and the
    // normalising denominator for the barycentric weights further down.
    const float area = edge_function(A.x, A.y, B.x, B.y, C.x, C.y);

    // Tallied before the cull, because the hardware sees these values too.
    TALLY(SIG_AREA,  area);
    TALLY(SIG_DELTA, B.x - A.x); TALLY(SIG_DELTA, B.y - A.y);
    TALLY(SIG_DELTA, C.x - B.x); TALLY(SIG_DELTA, C.y - B.y);
    TALLY(SIG_DELTA, A.x - C.x); TALLY(SIG_DELTA, A.y - C.y);

    // Backface cull. The signed area's sign is the triangle's winding as seen
    // from the camera: a triangle rotating away passes through zero, edge-on,
    // and emerges with the sign reversed. The projection has already done the
    // work; this only reads the result.
    //
    // Negative is front-facing here. MEASURED, NOT DERIVED -- the pipeline
    // contains two candidate reversals (the viewport Y flip and the sign of w)
    // and reasoning through them gave the wrong answer. The cube at zero
    // rotation settles it: its single visible face is the only negative pair,
    // and the magnitudes check out, with the front face projecting
    // (5/3)^2 = 2.778 times the area of the back one and 388800/139968 = 2.778.
    //
    // >= rather than > also drops degenerate zero-area triangles, which would
    // divide by zero when normalising the barycentric weights below.
    //
    // This is the cheapest work-elimination in the pipeline: one comparison at
    // the narrow per-triangle end removes an entire triangle's worth of work at
    // the wide per-fragment end, which is why real hardware culls in
    // fixed-function logic between the vertex stage and the rasterizer. The
    // bit-width study found the same trade a second time: bounding triangle
    // screen extent upstream would buy four bits in the edge accumulator.
    if (area >= 0.0f)
        return;

    int min_x, min_y, max_x, max_y;
    bounding_box(A.x, A.y, B.x, B.y, C.x, C.y, min_x, min_y, max_x, max_y);

    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {

            // Sample at the pixel CENTRE. Screen coordinates are floats and
            // only centres sit on the integer grid; testing at (x, y) would
            // sample the corner and shift coverage by half a pixel.
            float w0 = edge_function(B.x, B.y, C.x, C.y, x + 0.5f, y + 0.5f);
            float w1 = edge_function(C.x, C.y, A.x, A.y, x + 0.5f, y + 0.5f);
            float w2 = edge_function(A.x, A.y, B.x, B.y, x + 0.5f, y + 0.5f);

            TALLY(SIG_EDGE, w0); TALLY(SIG_EDGE, w1); TALLY(SIG_EDGE, w2);

            // Inside if the sample is on the same side of all three edges.
            //
            // The all-positive branch is now unreachable: w0 + w1 + w2 == area
            // identically, and the cull above guarantees area < 0, so three
            // non-negative weights cannot occur. It is kept as a statement of
            // the general test rather than one specialised to the current
            // winding convention.
            const bool inside = (w0 >= 0 && w1 >= 0 && w2 >= 0) ||
                                (w0 <= 0 && w1 <= 0 && w2 <= 0);

            if (inside) {
                // Each w is twice the area of the sub-triangle opposite its
                // vertex. Dividing by the total area turns them into
                // barycentric weights summing to 1.
                //
                // NORMALISE BEFORE INTERPOLATING DEPTH. Interpolating with the
                // raw weights scales z by the triangle's area, which makes the
                // depth test area-dependent. It looks correct on simple scenes
                // by coincidence and fails on interpenetrating geometry -- which
                // the solids scene deliberately has.
                w0 = w0 / area;
                w1 = w1 / area;
                w2 = w2 / area;

                // Measured distribution of these is exactly the 2(1-w) density
                // predicted for a uniformly sampled point in a triangle, to four
                // significant figures on every mesh. Since barycentric
                // coordinates are affine-invariant the result is
                // mesh-independent -- which is how a mislabelled tally site was
                // caught: three unrelated meshes produced identical histograms,
                // and a quantity identical across unrelated meshes is almost
                // never geometry.
                TALLY(SIG_BARY, w0); TALLY(SIG_BARY, w1); TALLY(SIG_BARY, w2);

                // Screen-space linear interpolation is EXACT for NDC depth:
                // NDC z is A + B/z_eye, so it is already a function of 1/z, and
                // 1/z is what varies linearly across a projected triangle.
                // Perspective-correcting this value would make it wrong.
                const float z = w0 * A.z + w1 * B.z + w2 * C.z;
                TALLY(SIG_Z, z);

                // Tallied BEFORE the branch, so it counts every comparison the
                // hardware performs rather than only the ones that win. Read
                // bottom-up: the smallest separation the comparator must resolve
                // sets the depth field's FRACTIONAL width, which is the opposite
                // of every other signal in the study.
                TALLY(SIG_ZDIFF, std::fabs(z - zbuffer[y * VIEWPORT_WIDTH + x]));

                if (z < zbuffer[y * VIEWPORT_WIDTH + x]) {
                    zbuffer[y * VIEWPORT_WIDTH + x] = z;

                    // Perspective-correct barycentrics, for attributes that are
                    // linear on the SURFACE rather than on the screen. Colour
                    // is one; depth above is not.
                    //
                    // Under projection a triangle's far half compresses into
                    // fewer pixels than its near half, so walking pixels at a
                    // uniform rate walks the surface at a non-uniform one. What
                    // does interpolate linearly in screen space is f/w and 1/w,
                    // so recovering f means interpolating both and dividing:
                    //
                    //     f = sum(w_i * f_i / w_i_clip) / sum(w_i / w_i_clip)
                    //
                    // Folding the denominator into the weights gives c0..c2,
                    // which still sum to 1 -- the denominator is exactly the
                    // sum of the numerators -- but are biased toward whichever
                    // vertex is nearer, in proportion to the depth ratio.
                    //
                    // One reciprocal, three multiplies. Computed here rather
                    // than above so occluded fragments do not pay for it. In
                    // hardware that is a divider on the critical path of every
                    // covered pixel rather than one per vertex, which is why the
                    // affine shortcut survived as long as it did.
                    //
                    // HAZARD: perspectiveTransform returns rec_w = 0 for a
                    // vertex at or behind the eye plane, and three of those
                    // make inv_w_pixel zero, the reciprocal infinite and each
                    // c NaN -- which the uint8_t cast below turns into
                    // undefined behaviour.
                    //
                    // The standard Q-format division rule cannot bound this: as
                    // the denominator goes to zero the quotient is unbounded, so
                    // the width depends on the SCENE's minimum |w| rather than
                    // on any format. Measurement over three meshes put it in
                    // [0.25, 2), so the guard has never fired -- but that bound
                    // is a property of the near plane, and without clipping
                    // there is no width that works.
                    const float inv_w_pixel = w0 * A.rec_w + w1 * B.rec_w + w2 * C.rec_w;
                    TALLY(SIG_INVW, inv_w_pixel);
                    if (std::fabs(inv_w_pixel) < 1e-8f)
                        continue;

                    const float inv_w_pixel_recip = 1.0f / inv_w_pixel;

                    const float c0 = w0 * A.rec_w * inv_w_pixel_recip;
                    const float c1 = w1 * B.rec_w * inv_w_pixel_recip;
                    const float c2 = w2 * C.rec_w * inv_w_pixel_recip;

                    // Gouraud: per-vertex colours interpolated across the face.
                    // The cast truncates rather than rounds, biasing every
                    // channel down by up to one level.
                    framebuffer[y * VIEWPORT_WIDTH + x] = {
                        std::uint8_t(c0 * A.color.r + c1 * B.color.r + c2 * C.color.r),
                        std::uint8_t(c0 * A.color.g + c1 * B.color.g + c2 * C.color.g),
                        std::uint8_t(c0 * A.color.b + c1 * B.color.b + c2 * C.color.b)
                    };
                }
            }
        }
    }
}