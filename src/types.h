#pragma once

/**
 * @file types.h
 * @brief Plain data types shared across the pipeline stages.
 *
 * Bottom of the dependency graph alongside vectors.h. Depends on \<cstdint\> and
 * nothing else, so any stage can include it without inheriting anything.
 *
 * The point of splitting these out: raster.h needs to know what a screenVertex
 * is, but not that a global framebuffer exists. Keeping the type definitions
 * separate from the buffers they end up in is what lets the rasterizer be
 * instantiated against a caller-supplied buffer -- which is exactly what the
 * RTL testbench will need when it diffs this rasterizer against the RTL one.
 *
 * raster.cpp still includes framebuffer.h, because the implementation does
 * write to the globals. Interface and bindings are different edges.
 */

#include <cstdint>

/**
 * @brief One pixel: 8 bits per channel, no alpha.
 *
 * Field order is the PPM wire order. This struct's BYTE LAYOUT is a contract
 * with the file format, not just a container -- writeFramebufferToPPM fwrites
 * the whole vector as raw bytes without ever naming .r/.g/.b.
 */
struct RGB {
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
};

/**
 * The compiler is free to insert padding between or after struct members for
 * alignment. Three uint8_t all have alignment 1, so no padding is possible in
 * practice -- but nothing in the language guarantees it, and if it ever
 * happened (a fourth channel added, a pragma, an unusual ABI) the raw fwrite
 * would shift every pixel after the first and produce diagonal garbage rather
 * than an error. This turns that silent corruption into a compile failure.
 *
 * Note this assert exists for RGB and not for Vec3 or Mat4: those are only ever
 * accessed by member name, so whatever offsets the compiler picks are resolved
 * correctly. Layout only matters when bytes cross a boundary -- file I/O, a bus
 * transaction, or a comparison against RTL output.
 */
static_assert(sizeof(RGB) == 3, "RGB must be tightly packed");

/**
 * @brief A vertex after projection and viewport transform, ready to rasterize.
 *
 * This is the handoff struct between the float front end and the fixed-point
 * back end, and the boundary is deliberate: real GPUs draw the same line.
 * Vertex processing is IEEE float because the MVP chain has unbounded dynamic
 * range; rasterization is fixed-point because coverage and depth must be exact
 * and reproducible, so two triangles sharing an edge compute bit-identical
 * values for it and no crack opens between them.
 *
 * @var screenVertex::x
 * @var screenVertex::y
 *      SCREEN coordinates in floats -- deliberately not integers. Only pixel
 *      centres sit on the integer grid, which is why sampling happens at
 *      (x + 0.5, y + 0.5). Rounding vertices here would quantise geometry and
 *      cost sub-pixel accuracy.
 *
 * @var screenVertex::z
 *      Post-divide NDC depth, used for the depth test only. Near maps to -1 and
 *      far to +1, so the smaller value is the nearer fragment.
 *
 * @var screenVertex::rec_w
 *      Reciprocal of the clip-space w that produced x, y and z -- the one piece
 *      of pre-divide information the rasterizer still needs, since
 *      perspective-correct interpolation is a weighted average of 1/w. Stored
 *      as the reciprocal because perspectiveTransform computes it once per
 *      vertex; storing w would mean recomputing it once per FRAGMENT.
 *
 * @var screenVertex::xi
 * @var screenVertex::yi
 *      x and y snapped to a 2^-g_frac_bits pixel grid, for the fixed-point
 *      rasterizer. Zero and unused when g_frac_bits < 0.
 *
 *      Snapped ONCE PER VERTEX, in the viewport transform, rather than inside
 *      drawTriangleFixed. Two triangles sharing edge AB then read bit-identical
 *      integers and compute a bit-identical edge function, so the shared edge
 *      lands in exactly the same place for both and no gap or double-cover
 *      appears. Snapping per triangle would give the same answer in practice
 *      but would not guarantee it, and that guarantee is the main thing fixed
 *      point buys over float.
 *
 * @var screenVertex::color
 *      The SHADED result, not a base colour: material diffuse times this
 *      vertex's Lambertian intensity, multiplied before rasterization. The base
 *      is per-TRIANGLE state from the material palette and only the intensity
 *      varies per vertex, so the three colours across one triangle share a hue
 *      and differ only in brightness. A shader-structured pipeline would carry
 *      the intensity alone and multiply per fragment -- one float across the
 *      span instead of three bytes.
 *
 * @note This is the vertex format a hardware fetch unit would read: four
 *       floats, two ints and three bytes, POD, flat, no indirection.
 */
struct screenVertex {
    float   x, y, z, rec_w;
    std::int32_t xi, yi;
    RGB     color;
};