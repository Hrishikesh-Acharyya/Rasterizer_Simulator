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
 * Per pixel that is covered and passes the depth test: 3 divisions, 4
 * interpolations, a depth WRITE and a colour WRITE.
 *
 * The read happens whether or not the pixel is covered, and consecutive pixels
 * share no data, so this is streaming traffic with no reuse. At 1080p that is
 * millions of accesses per frame per triangle-area, and it dominates the
 * bandwidth budget long before the arithmetic does.
 *
 * DEFERRED: `area` is constant across the whole triangle, so 1.0f/area could be
 * hoisted out of the loop and the three divisions become three multiplies.
 * Not done here because reciprocal-multiply and division round differently in
 * floating point, so it changes output bit-for-bit and belongs in its own
 * commit with its own verification.
 */
void drawTriangle(const screenVertex& A, const screenVertex& B, const screenVertex& C) {

    int min_x, min_y, max_x, max_y;
    bounding_box(A.x, A.y, B.x, B.y, C.x, C.y, min_x, min_y, max_x, max_y);

    // Signed area of the whole triangle: the normalising denominator for the
    // barycentric weights below.
    float area = edge_function(A.x, A.y, B.x, B.y, C.x, C.y);

    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {

            // Sample at the pixel CENTRE. Screen coordinates are floats and
            // only centres sit on the integer grid; testing at (x, y) would
            // sample the corner and shift coverage by half a pixel.
            float w0 = edge_function(B.x, B.y, C.x, C.y, x + 0.5f, y + 0.5f);
            float w1 = edge_function(C.x, C.y, A.x, A.y, x + 0.5f, y + 0.5f);
            float w2 = edge_function(A.x, A.y, B.x, B.y, x + 0.5f, y + 0.5f);

            // Inside if the sample is on the same side of all three edges.
            // Accepting all-negative as well as all-positive makes this
            // winding-agnostic, at the cost of giving up the sign that would
            // otherwise identify a back-facing triangle for free.
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

                float z = w0 * A.z + w1 * B.z + w2 * C.z;

                if (z < zbuffer[y * VIEWPORT_WIDTH + x]) {
                    zbuffer[y * VIEWPORT_WIDTH + x] = z;

                    // Gouraud: per-vertex colours interpolated across the face.
                    //
                    // This interpolation is screen-space linear, which is
                    // correct for depth in NDC but NOT for attributes under
                    // perspective -- a receding surface's attributes should
                    // vary non-linearly across the screen. Invisible on small
                    // triangles, obvious on large ones at oblique angles.
                    // Perspective-correct interpolation divides through by w.
                    framebuffer[y * VIEWPORT_WIDTH + x] = {
                        std::uint8_t(w0 * A.color.r + w1 * B.color.r + w2 * C.color.r),
                        std::uint8_t(w0 * A.color.g + w1 * B.color.g + w2 * C.color.g),
                        std::uint8_t(w0 * A.color.b + w1 * B.color.b + w2 * C.color.b)
                    };
                }
            }
        }
    }
}