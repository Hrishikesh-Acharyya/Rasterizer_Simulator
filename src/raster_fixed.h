#pragma once

/**
 * @file raster_fixed.h
 * @brief Fixed-point coverage path, for the sub-pixel precision study.
 *
 * A second rasterizer sitting beside the float one rather than replacing it.
 * Only COVERAGE is fixed-point here: vertex snapping, the edge function and the
 * inside test. Depth interpolation, the depth test, perspective correction and
 * colour blending are copied verbatim from drawTriangle and still run in float.
 *
 * That split is the whole point of the experiment. Any pixel that differs from
 * the golden reference is unambiguously a coverage decision that flipped, and
 * nothing else. Depth and colour quantisation are separate questions with
 * separate answers, and mixing them would make a difference impossible to
 * attribute.
 *
 * Selection is a RUNTIME parameter, not a compile-time one, because the
 * fractional bit count is the swept variable: one binary must produce the whole
 * error-versus-width curve. main.cpp dispatches on the global g_frac_bits.
 *
 * @see raster.h for the golden reference.
 */

#include <cstdint>

#include "types.h"

/**
 * @brief Signed area of triangle ABC in fixed point.
 *
 * @param ax,ay  First vertex of the directed edge, Q13.s.
 * @param bx,by  Second vertex of the directed edge, Q13.s.
 * @param cx,cy  Point being classified against edge AB, Q13.s.
 * @return Twice the signed area, Q29.2s. Divide by 2^(2s) to read it back in
 *         pixel^2 units; the inside test only needs the sign.
 *
 * Format propagation, and why the return type is 64-bit:
 *
 *   - A screen coordinate is at most 1920, so 11 magnitude bits plus sign plus
 *     one spare gives Q13.s. At s = 16 that is 29 bits -- fits int32_t.
 *   - A difference of two adds one guard bit: Q14.s, 30 bits at s = 16. Still
 *     fits int32_t, which is why the subtractions below are not widened.
 *   - A PRODUCT adds widths: Q14.s * Q14.s -> Q28.2s, which is 36 bits even at
 *     s = 4 and 60 bits at s = 16. This is the operation that forces 64 bits.
 *   - The final subtraction adds one more guard bit: Q29.2s, 61 bits at s = 16.
 *
 * That last figure is why main.cpp caps the argument at 16; s = 22 would need
 * 73 bits and silently overflow int64_t.
 *
 * @note Measurement beats propagation here. The histogram study found the
 *       integer field needs 19 bits rather than the 29 the rule predicts, so
 *       W = 19 + 2s: 27 bits at s = 4. In C there is no 27-bit type and the
 *       code uses int64_t regardless, but in RTL that is reg signed [26:0] and
 *       the ten bits are real silicon.
 */
std::int64_t edge_function_fixed(std::int32_t ax, std::int32_t ay,
                                 std::int32_t bx, std::int32_t by,
                                 std::int32_t cx, std::int32_t cy);

/**
 * @brief Rasterize one triangle with a fixed-point coverage test.
 *
 * @param A,B,C  Screen-space vertices. Reads the snapped xi/yi fields for
 *               coverage and the float z/rec_w/color fields for everything
 *               after the inside test.
 * @param s      Sub-pixel fractional bits, 0..16. Must match the value used to
 *               fill xi/yi in the viewport transform, or coverage is computed
 *               against a grid the vertices were never snapped to.
 *
 * @pre A.xi, A.yi and the same fields on B and C were filled by snapping at
 *      this same @p s. Passing a different @p s than the one used for snapping
 *      is not detected and produces silently wrong coverage.
 *
 * @warning At s = 0 there is no representation of half a pixel, so the sample
 *          point falls on the pixel CORNER rather than its centre. That is a
 *          systematic half-pixel shift on top of the snapping error, and it is
 *          why s = 0 sits off the 2^-s error curve in every measurement.
 *
 * @see drawTriangle for the float path this mirrors.
 */
void drawTriangleFixed(const screenVertex& A, const screenVertex& B,
                       const screenVertex& C, int s);