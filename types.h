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
 * RTL testbenches in future  will need when it diffs this rasterizer against the RTL one.
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
struct RGB{

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
 * correctly. Layout only matters when bytes cross a boundary -- file I/O, a
 * bus transaction, or a comparison against RTL output.
 */
static_assert(sizeof(RGB) == 3, "RGB must be tightly packed");

/**
 * @brief A vertex after projection and viewport transform, ready to rasterize.
 *
 * x, y are SCREEN coordinates in floats -- deliberately not integers. Only
 * pixel centres sit on the integer grid, which is why sampling happens at
 * (x + 0.5, y + 0.5). Rounding vertices here would quantise geometry and cost
 * sub-pixel accuracy.
 *
 * z is post-divide NDC depth, used for the depth test only. Near maps to -1 and
 * far to +1, so the smaller value is the nearer fragment.
 *
 * rec_w is the reciprocal of the clip-space w that produced x, y and z -- the
 * one piece of pre-divide information the rasterizer still needs, since
 * perspective-correct interpolation is a weighted average of 1/w. Stored as the
 * reciprocal because perspectiveTransform computes it once per vertex and
 * storing w would mean recomputing it once per FRAGMENT.
 *
 * color is the shaded result, not a base colour: material diffuse times this
 * vertex's Lambertian intensity, multiplied before rasterization. The base is
 * per-TRIANGLE state from the material palette and only the intensity varies
 * per vertex, so the three colours across one triangle share a hue and differ
 * only in brightness. A shader-structured pipeline would carry the intensity
 * alone and multiply per fragment -- one float across the span instead of
 * three bytes.
 *
 * This is the vertex format a hardware fetch unit would read: four floats and
 * three bytes, POD, flat, no indirection.
 */

struct screenVertex{
float x,y,z,rec_w;
RGB color;

};

