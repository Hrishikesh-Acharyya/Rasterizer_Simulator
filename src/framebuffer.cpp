/**
 * @file framebuffer.cpp
 * @brief Definitions for the frame and depth buffers and their operations.
 *
 * This is the single translation unit where the two buffers actually exist.
 * framebuffer.h only DECLARES them (extern); the lines below are what allocate
 * the memory. Duplicating these into a header would give every including .cpp
 * its own 6 MB allocation under the same name, and the linker would reject the
 * program. That constraint is what forces this file to exist -- the functions
 * follow the data.
 */

#include "framebuffer.h"
#include <cstdio>    
#include <limits> 
#include <string>   

/**
 * Colour buffer: 1920*1080 pixels * 3 bytes = 6,220,800 bytes.
 *
 * Row-major, index [y * VIEWPORT_WIDTH + x]. Row 0 is the TOP of the screen,
 * which is why the viewport transform flips Y on the way in from NDC.
 */
std::vector<RGB> framebuffer(no_of_pixels);

/**
 * Depth buffer: one float per pixel, 8,294,400 bytes -- larger than the colour
 * buffer it serves, which is the first hint that depth traffic dominates.
 *
 * Initialised to +infinity so that the first fragment reaching any pixel always
 * wins the `z < zbuffer[i]` comparison. Using the largest finite float instead
 * would work here but breaks the moment a depth value legitimately equals it.
 */
std::vector<float> zbuffer(no_of_pixels, std::numeric_limits<float>::infinity());

/**
 * @brief Fill the colour buffer with the background grey.
 *
 * The nested loop computes y*WIDTH + x per pixel to reach addresses that are
 * already consecutive: 2 million multiply-adds to walk a flat array in order.
 * std::fill expresses the same thing and lets the compiler emit a vectorised
 * block store. Deliberately left as-is until the file split is verified as a
 * no-op; changing behaviour and structure in one step means an unexplained
 * difference has two possible causes.
 *
 */
void clear_frameBuffer() {
    for (int y = 0; y < VIEWPORT_HEIGHT; ++y) {
        for (int x = 0; x < VIEWPORT_WIDTH; ++x) {
            framebuffer[y * VIEWPORT_WIDTH + x] = {uint8_t(125), uint8_t(125), uint8_t(125)};
        }
    }
}

/**
 * @brief Reset every depth entry to +infinity.
 *
 * Must run before every frame. Skipping it leaves the previous frame's depths
 * in place, which rejects most of the new frame's fragments -- the model
 * appears to be occluded by a ghost of where it used to be.
 *
 */
void clear_zBuffer() {
    for (int i = 0; i < no_of_pixels; i++) {
        zbuffer[i] = std::numeric_limits<float>::infinity();
    }
}

/**
 * @brief Write the colour buffer as a binary PPM (P6).
 *
 * Format is an ASCII header -- magic, width, height, max channel value, each
 * whitespace-separated -- followed immediately by raw RGB bytes in row order,
 * top row first. No compression, no palette, no metadata. Chosen because it
 * needs no library and the header can be read with `head -c 20`, so a corrupt
 * file can be diagnosed by hand.
 *
 * The fwrite hands the vector's raw bytes straight to the OS: nothing here ever
 * names .r, .g or .b. That is what the static_assert in the header protects --
 * if RGB were ever padded, every pixel after the first would shift by the
 * padding amount and the image would come out as diagonal streaks.
 *
 */
bool writeFramebufferToPPM(const std::string& filename) {
    FILE* f = std::fopen(filename.c_str(), "wb");   // "wb": binary mode matters
                                                    // on Windows, where text
                                                    // mode rewrites 0x0A bytes
                                                    // and corrupts pixel data
    if (f == nullptr) {
        std::printf("Error: could not open %s for writing\n", filename.c_str());
        return false;
    }

    std::fprintf(f, "P6\n%d %d\n255\n", VIEWPORT_WIDTH, VIEWPORT_HEIGHT);
    std::fwrite(framebuffer.data(), sizeof(RGB), no_of_pixels, f);
    std::fclose(f);
    return true;
}