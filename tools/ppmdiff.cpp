/**
 * @file ppmdiff.cpp
 * @brief Compare two sequences of binary PPM frames and report how far apart.
 *
 * Standalone: not part of the renderer, links against nothing in it.
 *
 *     ppmdiff <dirA> <dirB> <frame_count>
 *
 * Expects @c dirA/000.ppm .. @c dirA/(N-1).ppm and the same in @c dirB, at
 * identical dimensions.
 *
 * The comparison this tool exists to support cannot ask "identical?". Rebuilding
 * the renderer with -O0 instead of -O2 already moves ~119,000 pixels per frame
 * on the solids scene at 1080p, because instruction selection changes the last
 * bit of a float and the truncating uint8_t cast turns that into a whole integer
 * step. A fixed-point rasterizer moves far more. So the question is "how far
 * apart, and where?", and the metrics below are chosen to answer it.
 *
 * PER-FRAME METRICS
 *
 *   diff        pixels where any channel differs
 *   maxch       largest single-channel difference, 0..255
 *   meanch      mean per-channel difference over DIFFERING pixels only
 *               (over all pixels it would be dominated by the identical ones)
 *   isolated    differing pixels with no differing pixel in their 8-neighbourhood
 *
 * The isolated fraction is the one that distinguishes two failure modes that a
 * raw pixel count cannot:
 *
 *   - A scatter of isolated pixels with large maxch is DEPTH TIE-BREAKING. Two
 *     near-coplanar surfaces are within one LSB of each other, the comparison
 *     flips, and if they carry different materials the pixel jumps by a hundred
 *     levels. Nothing is wrong; the geometry is ambiguous at that precision.
 *
 *   - Contiguous runs along triangle edges are COVERAGE SHIFT. Vertex snapping
 *     moved an edge line, and the pixels along it changed owner together. This
 *     is the quantity phase 3 is measuring, and it should scale as 2^-s.
 *
 * A build that is correct shows the first and a predictable amount of the
 * second. A build that is broken shows neither pattern.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cinttypes>   // PRIu64 -- MinGW links MSVCRT, whose printf rejects %llu
#include <vector>

struct Image {
    int width  = 0;
    int height = 0;
    std::vector<std::uint8_t> px;   // 3 bytes per pixel, PPM wire order
};

/**
 * Skip whitespace and #-comments, then read one non-negative integer.
 * PPM headers are whitespace-delimited with comments legal between any two
 * tokens, so this cannot be done with a single fscanf format.
 */
static bool readHeaderInt(std::FILE* f, int& out) {
    int c;
    for (;;) {
        c = std::fgetc(f);
        if (c == EOF) return false;
        if (c == '#') {                       // comment runs to end of line
            while (c != '\n' && c != EOF) c = std::fgetc(f);
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
        break;
    }
    if (c < '0' || c > '9') return false;
    int v = 0;
    while (c >= '0' && c <= '9') {
        v = v * 10 + (c - '0');
        c = std::fgetc(f);
    }
    out = v;
    return true;
}

static bool loadPPM(const char* path, Image& img) {
    std::FILE* f = std::fopen(path, "rb");     // "rb" matters on Windows: without
    if (!f) return false;                      // it, 0x0A bytes get mangled

    char magic[3] = {0};
    if (std::fread(magic, 1, 2, f) != 2 || magic[0] != 'P' || magic[1] != '6') {
        std::fclose(f);
        std::fprintf(stderr, "%s: not a binary PPM (P6)\n", path);
        return false;
    }

    int maxval = 0;
    if (!readHeaderInt(f, img.width) ||
        !readHeaderInt(f, img.height) ||
        !readHeaderInt(f, maxval)) {
        std::fclose(f);
        std::fprintf(stderr, "%s: malformed header\n", path);
        return false;
    }
    if (maxval != 255) {
        std::fclose(f);
        std::fprintf(stderr, "%s: maxval %d, only 255 supported\n", path, maxval);
        return false;
    }
    // No extra read here: readHeaderInt stops by consuming the first non-digit
    // byte, which IS the single whitespace the spec puts before the pixel data.
    // An fgetc at this point would eat the first byte of the image.

    const size_t n = (size_t)img.width * img.height * 3;
    img.px.resize(n);
    if (std::fread(img.px.data(), 1, n, f) != n) {
        std::fclose(f);
        std::fprintf(stderr, "%s: truncated pixel data\n", path);
        return false;
    }
    std::fclose(f);
    return true;
}

int main(int argc, char** argv) {
    if (argc != 4) {
        std::fprintf(stderr, "usage: %s <dirA> <dirB> <frame_count>\n", argv[0]);
        return 2;
    }
    const char* dirA = argv[1];
    const char* dirB = argv[2];
    const int   N    = std::atoi(argv[3]);

    // Totals across the whole sequence.
    std::uint64_t tot_diff = 0, tot_iso = 0, tot_chsum = 0, tot_chcount = 0;
    int tot_maxch = 0, frames = 0;

    // Distribution of per-pixel max-channel difference. Bucket 0 is unused
    // (identical pixels are not counted), so the useful range is 1..255.
    std::uint64_t hist[256] = {0};

    std::printf("frame,diff,pct,maxch,meanch,isolated,iso_pct\n");

    for (int i = 0; i < N; ++i) {
        char pa[512], pb[512];
        std::snprintf(pa, sizeof(pa), "%s/%03d.ppm", dirA, i);
        std::snprintf(pb, sizeof(pb), "%s/%03d.ppm", dirB, i);

        Image a, b;
        if (!loadPPM(pa, a) || !loadPPM(pb, b)) return 1;
        if (a.width != b.width || a.height != b.height) {
            std::fprintf(stderr, "frame %d: %dx%d vs %dx%d\n",
                         i, a.width, a.height, b.width, b.height);
            return 1;
        }

        const int W = a.width, H = a.height;
        const size_t npx = (size_t)W * H;

        // Pass 1: per-pixel max-channel difference. Kept as a byte array so
        // pass 2 can look at neighbours without re-reading both images.
        std::vector<std::uint8_t> d(npx, 0);

        std::uint64_t f_diff = 0, f_chsum = 0;
        int f_maxch = 0;

        for (size_t p = 0; p < npx; ++p) {
            int m = 0, s = 0;
            for (int c = 0; c < 3; ++c) {
                int delta = (int)a.px[p*3 + c] - (int)b.px[p*3 + c];
                if (delta < 0) delta = -delta;
                if (delta > m) m = delta;
                s += delta;
            }
            if (m) {
                d[p] = (std::uint8_t)m;
                ++f_diff;
                f_chsum += (unsigned)s;
                if (m > f_maxch) f_maxch = m;
                ++hist[m];
            }
        }

        // Pass 2: a differing pixel is isolated if none of its 8 neighbours
        // also differs. Border pixels are treated as having fewer neighbours
        // rather than being skipped -- an edge artifact is still an artifact.
        std::uint64_t f_iso = 0;
        if (f_diff) {
            for (int y = 0; y < H; ++y) {
                for (int x = 0; x < W; ++x) {
                    if (!d[(size_t)y*W + x]) continue;
                    bool alone = true;
                    for (int dy = -1; dy <= 1 && alone; ++dy) {
                        for (int dx = -1; dx <= 1; ++dx) {
                            if (!dx && !dy) continue;
                            const int nx = x + dx, ny = y + dy;
                            if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
                            if (d[(size_t)ny*W + nx]) { alone = false; break; }
                        }
                    }
                    if (alone) ++f_iso;
                }
            }
        }

        const double pct    = 100.0 * (double)f_diff / (double)npx;
        const double meanch = f_diff ? (double)f_chsum / (3.0 * (double)f_diff) : 0.0;
        const double isopct = f_diff ? 100.0 * (double)f_iso / (double)f_diff : 0.0;

        std::printf("%d,%" PRIu64 ",%.4f,%d,%.3f,%" PRIu64 ",%.1f\n",
                    i, f_diff, pct, f_maxch, meanch, f_iso, isopct);

        tot_diff    += f_diff;
        tot_iso     += f_iso;
        tot_chsum   += f_chsum;
        tot_chcount += f_diff * 3;
        if (f_maxch > tot_maxch) tot_maxch = f_maxch;
        ++frames;
    }

    if (!frames) return 0;

    std::fprintf(stderr, "\n--- %d frames ---\n", frames);
    std::fprintf(stderr, "differing pixels : %" PRIu64 "  (%.1f per frame)\n",
                 tot_diff, (double)tot_diff / frames);
    std::fprintf(stderr, "max channel diff : %d\n", tot_maxch);
    std::fprintf(stderr, "mean channel diff: %.3f  (over differing pixels)\n",
                 tot_chcount ? (double)tot_chsum / (double)tot_chcount : 0.0);
    std::fprintf(stderr, "isolated         : %" PRIu64 "  (%.1f%%)\n",
                 tot_iso, tot_diff ? 100.0 * (double)tot_iso / (double)tot_diff : 0.0);

    std::fprintf(stderr, "\nmax-channel-difference distribution:\n");
    for (int m = 1; m < 256; ++m)
        if (hist[m])
            std::fprintf(stderr, "  %3d %12" PRIu64 "\n", m, hist[m]);

    return 0;
}