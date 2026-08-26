/**
 * @file raster.cpp
 * @brief Rasterizer implementation.
 *
 * Includes framebuffer.h, which raster.h deliberately does not: the
 * implementation writes to the global buffers, the interface does not need to
 * know they exist.
 */

#include "raster.h"
#include "framebuffer.h"
#include <algorithm>   // std::min, std::max, initialiser-list overloads
#include <cstdint>     // std::uint8_t
#include <cmath>       // std::floor, std::ceil

/**
 * Two multiplies and five adds. No division, no branch, no memory access.
 *
 * The whole coverage test is three of these plus three sign comparisons, and
 * the cost is identical for every pixel regardless of geometry. That uniformity
 * is what makes rasterization parallelisable: no pixel's result depends on any
 * other's, so an arbitrary number can be evaluated at once.
 */
float edge_function(float ax, float ay, float bx, float by, float cx, float cy) {
    return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
}

void bounding_box(float ax, float ay, float bx, float by, float cx, float cy,
                  int& min_x, int& min_y, int& max_x, int& max_y) {
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
 * Per pixel in the bounding box: 3 edge functions and one depth-buffer READ.
 * Per covered pixel: 3 divisions to normalise the weights, plus the depth
 * interpolation. Per pixel that also PASSES the depth test: one more reciprocal
 * for the perspective correction, 3 multiplies, 3 colour interpolations, a
 * depth WRITE and a colour WRITE.
 *
 * The read happens whether or not the pixel is covered, and consecutive pixels
 * share no data, so this is streaming traffic with no reuse. At 1080p that is
 * millions of accesses per frame, and it dominates the bandwidth budget long
 * before the arithmetic does.
 *
 * The ordering above is deliberate: work moves below the depth test wherever it
 * can, so an occluded fragment pays for coverage and depth and nothing else.
 * On a self-occluding mesh that is a large fraction of covered fragments.
 *
 * DEFERRED: `area` is constant across the whole triangle, so 1.0f/area could be
 * hoisted out of the loop and the three divisions become three multiplies.
 * Not done here because reciprocal-multiply and division round differently in
 * floating point, so it changes output bit-for-bit and belongs in its own
 * commit with its own verification.
 */
void drawTriangle(const screenVertex& A, const screenVertex& B, const screenVertex& C) {

    // Signed area of the whole triangle: the normalising denominator for the
    // barycentric weights below, and the facing test above.
    float area = edge_function(A.x, A.y, B.x, B.y, C.x, C.y);

    // Backface cull. The signed area's sign is the triangle's winding as seen
    // from the camera: a triangle rotating away passes through zero, edge-on,
    // and emerges with the sign reversed. The projection has already done the
    // work; this only reads the result.
    //
    // Negative is front-facing here. Measured, not derived -- the pipeline
    // contains two candidate reversals (the viewport Y flip and the sign of w)
    // and reasoning through them gave the wrong answer. The cube at zero
    // rotation shows its single visible face as the only negative pair, and the
    // magnitudes check out: the front face projects (5/3)^2 = 2.778 times the
    // area of the back one at camera distance 4, and 388800/139968 = 2.778.
    //
    // >= rather than > also drops degenerate zero-area triangles, which would
    // divide by zero when normalising the barycentric weights below.
    //
    // This is the cheapest work-elimination in the pipeline: one comparison at
    // the per-triangle end removes an entire triangle's worth of per-fragment
    // work at the wide end, which is why real hardware culls in fixed-function
    // logic between the vertex stage and the rasterizer.
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

            // Inside if the sample is on the same side of all three edges.
            //
            // The all-positive branch is now unreachable: w0 + w1 + w2 == area
            // identically, and the cull above guarantees area < 0, so three
            // non-negative weights cannot occur. It is kept as a statement of
            // the general test rather than one specialised to the current
            // winding convention.
            bool inside = (w0 >= 0 && w1 >= 0 && w2 >= 0) ||
                          (w0 <= 0 && w1 <= 0 && w2 <= 0);

            if (inside) {
                // Each w is twice the area of the sub-triangle opposite its
                // vertex. Dividing by the total area turns them into
                // barycentric weights summing to 1.
                //
                // NORMALISE BEFORE INTERPOLATING DEPTH. Interpolating with the
                // raw weights scales z by the triangle's area, which makes the
                // depth test area-dependent. It looks correct on simple scenes
                // by coincidence and fails on interpenetrating geometry.
                w0 = w0 / area;
                w1 = w1 / area;
                w2 = w2 / area;

                // Screen-space linear interpolation is EXACT for NDC depth:
                // NDC z is A + B/z_eye, so it is already a function of 1/z, and
                // 1/z is what varies linearly across a projected triangle.
                // Perspective-correcting this value would make it wrong.
                float z = w0 * A.z + w1 * B.z + w2 * C.z;

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
                    // than above so occluded fragments do not pay for it.
                    //
                    // HAZARD: perspectiveTransform returns rec_w = 0 for a
                    // vertex at or behind the eye plane, and three of those
                    // make inv_w_pixel zero, the reciprocal infinite and each
                    // c NaN -- which the uint8_t cast below turns into
                    // undefined behaviour. Unreachable while the camera stays
                    // outside the model; the real fix is near-plane clipping.
                    float inv_w_pixel = w0 * A.rec_w + w1 * B.rec_w + w2 * C.rec_w;
                    if (std::fabs(inv_w_pixel) < 1e-8f)
                         continue;

                    float inv_w_pixel_recip = 1.0f / inv_w_pixel;

                    float c0 = w0 * A.rec_w * inv_w_pixel_recip;
                    float c1 = w1 * B.rec_w * inv_w_pixel_recip;
                    float c2 = w2 * C.rec_w * inv_w_pixel_recip;

                    // Gouraud: per-vertex colours interpolated across the face.
                    framebuffer[y * VIEWPORT_WIDTH + x] = {
                        std::uint8_t(c0 * A.color.r + c1 * B.color.r + c2 * C.color.r),
                        std::uint8_t(c0 * A.color.g + c1 * B.color.g + c2 * C.color.g),
                        std::uint8_t(c0 * A.color.b + c1 * B.color.b + c2 * C.color.b)
                    };

                //     framebuffer[y * VIEWPORT_WIDTH + x] = {
                //         std::uint8_t(w0 * A.color.r + w1 * B.color.r + w2 * C.color.r),
                //         std::uint8_t(w0 * A.color.g + w1 * B.color.g + w2 * C.color.g),
                //         std::uint8_t(w0 * A.color.b + w1 * B.color.b + w2 * C.color.b)
                //     };
                }
            }
        }
    }
}