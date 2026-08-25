#pragma once

/**
 * @file framebuffer.h
 * @brief Frame and depth buffer storage, clear operations, and PPM output.
 *
 * First header in the project with a matching .cpp. The pattern:
 * declarations here, definitions there. main.cpp is compiled with no knowledge
 * of framebuffer.cpp -- this header is the only thing it sees, and the linker
 * matches the calls against the bodies afterwards.
 *
 * The two buffers are globals, which is how the code currently works and is
 * preserved here so the file split stays a no-op. It is not the right shape:
 * a Framebuffer struct passed explicitly would make ownership and lifetime
 * visible. In hardware terms a framebuffer is a memory region with a base
 * address and a size, not an ambient fact -- which is exactly what the SDRAM
 * controller in chunk 3 has to make explicit.
 */

#include <vector>
#include <cstdint>
#include <string>
constexpr int VIEWPORT_WIDTH  = 1920;
constexpr int VIEWPORT_HEIGHT = 1080;
constexpr int no_of_pixels =   VIEWPORT_HEIGHT*VIEWPORT_WIDTH;


/**
 * @brief One pixel: 8 bits per channel, no alpha.
 *
 * Field order is the PPM wire order. This struct's BYTE LAYOUT is a contract
 * with the file format, not just a container -- writeFramebufferToPPM fwrites
 * the whole vector as raw bytes without ever naming .r/.g/.b.
 */
struct RGB{

uint8_t r;
uint8_t g;
uint8_t b;
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
 * @brief Colour buffer, row-major, no_of_pixels entries. Index [y*WIDTH + x].
 * Row-major with row 0 at the TOP, which is why the viewport transform flips Y:
 * NDC has +Y up, the framebuffer has +Y down.
 */
extern std::vector<RGB> framebuffer;//Each RGB struct is one pixel

/**
 * @brief Depth buffer, one float per pixel, same indexing as framebuffer.
 *
 * Cleared to +infinity so the first fragment at any pixel always passes the
 * `z < zbuffer[i]` test. Storing depth separately from colour is the standard
 * split, and the bandwidth consequence is: every fragment
 * costs a depth READ, and only surviving fragments cost a depth write plus a
 * colour write. At 1080p that is millions of accesses per frame with no reuse
 * between them - streaming, not cached. This is the traffic that makes a GPU
 * a memory bandwidth problem, and the reason real hardware spends transistors
 * on hierarchical-Z and depth compression rather than on more ALUs.
 */
extern std::vector<float> zbuffer; 

/// Fill the colour buffer with the background grey. Called once per frame.
void clear_frameBuffer();

/// Reset every depth entry to +infinity. Must run before each frame, or the
/// previous frame's depths reject this frame's fragments.
void clear_zBuffer();

/**
 * @brief Write the colour buffer as a binary PPM (P6).
 *
 * P6 is a ~15-byte ASCII header followed by raw RGB bytes -- chosen because it
 * needs no library and the file can be verified by hand. Correctness depends on
 * the static_assert above holding.
 */
void writeFramebufferToPPM(const std::string& filename);