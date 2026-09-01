/**
 * @file stats.cpp
 * @brief Exponent histogram implementation.
 *
 * The whole file is inside the switch, so the object is empty in a normal
 * build. Includes nothing from the project beyond its own header, which keeps
 * it at the bottom of the dependency graph beside vectors.h and types.h.
 */

#include "stats.h"

#if HISTOGRAM_STATS

#include <cinttypes>   // PRIu64
#include <cmath>       // std::ilogb, std::isfinite
#include <cstdint>
#include <cstdio>

/// Bin @c i holds values with ilogb == i - BIN_OFFSET, i.e. |v| in [2^e, 2^(e+1)).
static const int BIN_OFFSET = 50;
static const int BIN_COUNT  = 100;

/**
 * Declared WITHOUT an explicit size so the static_assert below can fire.
 * Writing `SIGNAL_NAMES[SIG_COUNT]` would make the division tautological, and a
 * missing entry would silently leave a null pointer for %s to dereference.
 */
static const char* SIGNAL_NAMES[] = {
    "edge", "z", "delta", "area", "zdiff", "screen", "recw", "invw", "bary"
};
static_assert(sizeof(SIGNAL_NAMES) / sizeof(SIGNAL_NAMES[0]) == SIG_COUNT,
              "SIGNAL_NAMES must have one entry per signal");

/**
 * Row-major, so [sig][bin] puts one signal's 100 bins in contiguous memory.
 * SIG_EDGE is tallied hundreds of millions of times and adjacent pixels land in
 * nearby bins, so the cache line is already hot; [bin][sig] would stride.
 *
 * uint64_t, not long: MinGW is LLP64 so long is 32 bits, and a 120-frame Iron
 * Man run recorded 2,136,285,247 edge tallies against an INT32_MAX of
 * 2,147,483,647 -- within 0.5% of overflow.
 */
static std::uint64_t tally     [SIG_COUNT][BIN_COUNT];
static std::uint64_t zero_count[SIG_COUNT];   ///< Exactly 0.0f; ilogb(0) is INT_MIN.
static std::uint64_t clamp_lo  [SIG_COUNT];   ///< |v| < 2^-50.
static std::uint64_t clamp_hi  [SIG_COUNT];   ///< |v| >= 2^50, or non-finite.

void stats_tally(StatSignal sig, float v)
{
    // Non-finite first: ilogb(inf) is INT_MAX and INT_MAX + BIN_OFFSET
    // overflows. Not a fault for SIG_ZDIFF, where the z-buffer is cleared to
    // +inf and the first fragment at each pixel legitimately lands here -- so
    // clamp_hi doubles as a distinct-pixels-covered counter there, and the
    // ratio of total tallies to it is average overdraw.
    if (!std::isfinite(v)) { ++clamp_hi[sig]; return; }

    // ilogb(0) is FP_ILOGB0, which is INT_MIN on GCC; INT_MIN + BIN_OFFSET is
    // signed overflow, i.e. undefined behaviour. Rare but not impossible: a
    // 120-frame Iron Man run recorded two exact-zero edge evaluations in 2.1
    // billion, and 169,640 exact-zero depth differences.
    if (v == 0.0f) { ++zero_count[sig]; return; }

    const int i = std::ilogb(v) + BIN_OFFSET;

    // The index derives from floating-point data, so it must be range-checked
    // before indexing. A nonzero clamp_hi on any signal other than SIG_ZDIFF
    // means a value exceeded 2^50 and something upstream is wrong; it is a bug
    // signal, not a data point.
    if (i < 0)          { ++clamp_lo[sig]; return; }
    if (i >= BIN_COUNT) { ++clamp_hi[sig]; return; }

    ++tally[sig][i];
}

void stats_dump(const char* prefix)
{
    for (int s = 0; s < SIG_COUNT; ++s) {
        char path[256];
        // snprintf, not sprintf: the buffer is fixed and the prefix comes from
        // a caller.
        std::snprintf(path, sizeof(path), "%s_%s.csv", prefix, SIGNAL_NAMES[s]);

        std::FILE* f = std::fopen(path, "w");
        // fopen returns NULL when the directory does not exist, and fprintf on
        // NULL is undefined behaviour. continue rather than return, so one bad
        // path does not lose the other signals.
        if (!f) { std::fprintf(stderr, "stats: cannot open %s\n", path); continue; }

        std::fprintf(f, "exponent,count\n");

        // Write the EXPONENT, not the array index: the offset is an
        // implementation detail and leaking it into the output makes the table
        // unreadable a week later. Empty bins are skipped -- the occupied range
        // is roughly 30 rows of 100.
        for (int i = 0; i < BIN_COUNT; ++i)
            if (tally[s][i])
                std::fprintf(f, "%d,%" PRIu64 "\n", i - BIN_OFFSET, tally[s][i]);

        // PRIu64 rather than %llu: MinGW links MSVCRT, whose printf rejects the
        // ll length modifier and wants %I64u. A rejected conversion also
        // desynchronises every argument after it.
        std::fprintf(f, "zero,%"     PRIu64 "\n", zero_count[s]);
        std::fprintf(f, "clamp_lo,%" PRIu64 "\n", clamp_lo[s]);
        std::fprintf(f, "clamp_hi,%" PRIu64 "\n", clamp_hi[s]);

        std::fclose(f);
    }
}

#endif  // HISTOGRAM_STATS