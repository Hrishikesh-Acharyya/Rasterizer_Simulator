/**
 * @file raster_fixed.cpp
 * @brief Fixed-point coverage path implementation.
 *
 * Structurally a copy of drawTriangle with four changes: the area and its cull
 * use integers, the sample point is built on the sub-pixel grid, the three edge
 * functions and the inside test are integer, and the weights convert back to
 * float once coverage has been decided. Everything below the inside test is
 * verbatim from raster.cpp and is meant to stay that way -- see raster_fixed.h
 * for why the split sits exactly there.
 */

#include "raster_fixed.h"

#include "framebuffer.h"   // zbuffer, framebuffer, VIEWPORT_*
#include "raster.h"        // bounding_box
#include <cassert>
#include <cmath>           // std::fabs
#include <cstdint>

/**
 * Range checks on the edge accumulator, independent of NDEBUG.
 *
 * These sit in the innermost loop -- three per fragment, which is ~2.2 billion
 * evaluations on a 10-frame Iron Man run -- so they are off by default. NDEBUG
 * would disable them, but it would also disable the loader invariant check in
 * main.cpp, which costs nothing and should stay. Hence a dedicated switch.
 *
 * Set to 1 when bringing up a new configuration or a new mesh. A fired assert
 * means either the vertices were snapped at a different s than the one passed
 * in, or the mesh contains a triangle larger than the histogram study measured.
 */
#ifndef RASTER_FIXED_RANGE_CHECKS
#define RASTER_FIXED_RANGE_CHECKS 0
#endif

#if RASTER_FIXED_RANGE_CHECKS
    /* Measured bound: W = 19 + 2s bits signed, so |E| < 2^(18 + 2s). The 19 is
     * the largest value observed across three meshes at 1080p, against 22 from
     * the geometric bound and 23 from naive Q propagation. */
    #define ASSERT_EDGE_FITS(e, s) \
        assert((e) > -(1LL << (18 + 2 * (s))) && (e) < (1LL << (18 + 2 * (s))))
#else
    #define ASSERT_EDGE_FITS(e, s) ((void)sizeof(e), (void)sizeof(s))
#endif

std::int64_t edge_function_fixed(std::int32_t ax, std::int32_t ay,
                                 std::int32_t bx, std::int32_t by,
                                 std::int32_t cx, std::int32_t cy)
{
    // The cast sits on an OPERAND, not on the result. C++ evaluates inside out,
    // so (int64_t)((bx-ax) * (cy-ay)) would complete the multiply in 32 bits,
    // overflow, and then widen an answer that is already wrong -- and signed
    // overflow is undefined behaviour, so the compiler is entitled to do worse
    // than wrap. Casting one operand promotes the other and forces a 64-bit
    // multiply.
    //
    // The subtractions are deliberately NOT widened: a delta is at most
    // 1920 * 2^16 = 126 million, comfortably inside int32_t. Only the product
    // needs the extra width.
    return (std::int64_t)(bx - ax) * (cy - ay)
         - (std::int64_t)(by - ay) * (cx - ax);
}

void drawTriangleFixed(const screenVertex& A, const screenVertex& B,
                       const screenVertex& C, int s)
{
    // Signed area of the whole triangle: the facing test below, and the
    // normalising denominator for the barycentric weights further down.
    const std::int64_t area_i =
        edge_function_fixed(A.xi, A.yi, B.xi, B.yi, C.xi, C.yi);

    // Backface cull. The signed area's sign is the triangle's winding as seen
    // from the camera: a triangle rotating away passes through zero, edge-on,
    // and emerges with the sign reversed. The projection has already done the
    // work; this only reads the result.
    //
    // Negative is front-facing here. Measured, not derived -- the pipeline
    // contains two candidate reversals (the viewport Y flip and the sign of w)
    // and reasoning through them gave the wrong answer.
    //
    // >= rather than > also drops degenerate zero-area triangles, which would
    // divide by zero when normalising the barycentric weights below. Note this
    // is an integer comparison: no epsilon, no tie-breaking ambiguity, which is
    // one of the things fixed point buys.
    if (area_i >= 0)
        return;

    // Half a pixel, on the sub-pixel grid: (x + 0.5) * 2^s == x * 2^s + 2^(s-1).
    //
    // At s == 0 there is no 2^-1 on the grid, so the sample lands on the pixel
    // CORNER instead of its centre. That is correct behaviour for a zero-bit
    // grid rather than a bug, and it is why s = 0 has two error sources where
    // every other s has one.
    const std::int32_t half = (s > 0) ? (1 << (s - 1)) : 0;

    // Bounding box from the UNSNAPPED floats. The box only has to contain the
    // triangle; one computed from float coordinates is at most half a sub-pixel
    // larger than one from the snapped integers, so it may test a few extra
    // pixels that the edge test then rejects. No pixel is ever missed, and
    // sharing the float version keeps any measured difference attributable to
    // coverage rather than to box arithmetic.
    int min_x, min_y, max_x, max_y;
    bounding_box(A.x, A.y, B.x, B.y, C.x, C.y, min_x, min_y, max_x, max_y);

    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {

            // Sample at the pixel centre, expressed on the sub-pixel grid.
            // Shift-and-or rather than multiply-and-add: identical value, but
            // the bit layout is explicit. x is at most 1920 and s at most 16,
            // so x << s reaches 126 million and stays inside int32_t.
            const std::int32_t px = (x << s) | half;
            const std::int32_t py = (y << s) | half;

            const std::int64_t e0 = edge_function_fixed(B.xi, B.yi, C.xi, C.yi, px, py);
            const std::int64_t e1 = edge_function_fixed(C.xi, C.yi, A.xi, A.yi, px, py);
            const std::int64_t e2 = edge_function_fixed(A.xi, A.yi, B.xi, B.yi, px, py);

            ASSERT_EDGE_FITS(e0, s);
            ASSERT_EDGE_FITS(e1, s);
            ASSERT_EDGE_FITS(e2, s);

            // Inside if the sample is on the same side of all three edges.
            //
            // THIS IS THE COVERAGE DECISION, and it is made entirely in
            // integers before any conversion back to float. Two triangles
            // sharing an edge read bit-identical snapped vertices, so they
            // compute bit-identical edge values and agree exactly about which
            // pixels lie on which side. That is the crack-freedom guarantee,
            // and it is why the vertices are snapped once per vertex rather
            // than once per triangle.
            //
            // The all-positive branch is unreachable given the cull above, but
            // is kept as a statement of the general test rather than one
            // specialised to the current winding convention.
            const bool inside = (e0 >= 0 && e1 >= 0 && e2 >= 0) ||
                                (e0 <= 0 && e1 <= 0 && e2 <= 0);

            if (inside) {
                // Normalised barycentric weights, in one division each.
                //
                // e and area_i both carry 2s fractional bits, so the scale
                // factors cancel exactly and no explicit 2^(2s) division is
                // needed. Doing it in two steps -- converting each to pixel^2
                // units and then dividing -- would cost three extra divisions
                // and three extra roundings for an identical result.
                //
                // Conversion happens only AFTER the inside test, so uncovered
                // pixels pay nothing for it.
                const float w0 = (float)e0 / (float)area_i;
                const float w1 = (float)e1 / (float)area_i;
                const float w2 = (float)e2 / (float)area_i;

                // --- everything below here is verbatim from drawTriangle ---

                // Screen-space linear interpolation is EXACT for NDC depth:
                // NDC z is A + B/z_eye, so it is already a function of 1/z, and
                // 1/z is what varies linearly across a projected triangle.
                // Perspective-correcting this value would make it wrong.
                const float z = w0 * A.z + w1 * B.z + w2 * C.z;

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
                    // HAZARD: perspectiveTransform returns rec_w = 0 for a
                    // vertex at or behind the eye plane, and three of those
                    // make inv_w_pixel zero, the reciprocal infinite and each
                    // c NaN -- which the uint8_t cast below turns into
                    // undefined behaviour. The histogram study measured this
                    // denominator over three meshes and found it never below
                    // 0.25, so the guard has never fired; the real fix is
                    // near-plane clipping, and the bound is a property of the
                    // clip plane rather than of any format.
                    const float inv_w_pixel = w0 * A.rec_w + w1 * B.rec_w + w2 * C.rec_w;
                    if (std::fabs(inv_w_pixel) < 1e-8f)
                        continue;

                    const float inv_w_pixel_recip = 1.0f / inv_w_pixel;

                    const float c0 = w0 * A.rec_w * inv_w_pixel_recip;
                    const float c1 = w1 * B.rec_w * inv_w_pixel_recip;
                    const float c2 = w2 * C.rec_w * inv_w_pixel_recip;

                    // Gouraud: per-vertex colours interpolated across the face.
                    //
                    // The cast TRUNCATES, which is the mechanism behind the
                    // irreducible one-level noise floor between this path and
                    // the float golden: a last-bit difference in a weight
                    // crosses an integer boundary and becomes a whole level.
                    // That floor scales inversely with tessellation density --
                    // ~139,000 px/frame on the 3,636-triangle solids scene
                    // against ~19,000 on the 217,038-triangle Iron Man.
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