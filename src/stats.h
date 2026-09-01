#pragma once

/**
 * @file stats.h
 * @brief Exponent histograms for fixed-point range analysis. One per signal.
 *
 * For each instrumented value the instrument records @c std::ilogb(v), the
 * integer @c e with 2^e <= |v| < 2^(e+1). That is the index of the most
 * significant set bit, so a value binned at @c e needs
 *
 *     W = e + 2       bits   (e + 1 magnitude, plus a sign)
 *
 * and the histogram IS the bit-width distribution, already computed. Reading
 * the cumulative sum from the top gives "W bits covers X% of all evaluations",
 * which is what turns a single worst-case number into a design space: on the
 * solids scene, 19 bits covers everything, 18 breaks 1.0%, 17 breaks 5.8%.
 *
 * @note @c ilogb reads the float's exponent field directly. It is not a
 *       logarithm and costs one instruction.
 *
 * ## Why the switch lives here rather than in the Makefile
 *
 * A @c -D flag changes no file's modification time, so make would hand back a
 * stale object. Every translation unit that instruments a signal includes this
 * header, so flipping the constant rebuilds all of them.
 *
 * ## Why the call sites carry no @c \#if
 *
 * @c TALLY expands in both configurations, so a typo at a call site fails to
 * compile whether the instrument is on or off. An earlier version guarded each
 * call site directly, and a misspelled variable survived three review passes
 * because the preprocessor deleted the line before the compiler saw it.
 *
 * When off, @c TALLY expands to an unevaluated @c sizeof: no code is emitted,
 * but the operands are still NAMED, so a variable that exists only to be
 * tallied does not trip @c -Wunused-variable under @c -Werror. It is also an
 * expression, so it parses in any statement position.
 */

/** Master switch. 0 for normal builds; 1 to regenerate the histogram CSVs. */
#define HISTOGRAM_STATS 0

/**
 * @brief Instrumented signals, one histogram each.
 *
 * Declared outside the @c \#if because @c TALLY names the enumerator in both
 * configurations. @c SIG_COUNT must stay last: it auto-numbers to one past the
 * final real signal, so array sizes and dump loops resize themselves when a
 * signal is inserted above it.
 */
enum StatSignal {
    SIG_EDGE   = 0,   ///< Raw w0/w1/w2, per bounding-box pixel. Sizes the coverage accumulator.
    SIG_Z      = 1,   ///< Interpolated NDC depth, per covered fragment. Sizes the depth field's range.
    SIG_DELTA,        ///< Six edge deltas per triangle. The multiplier INPUTS, versus SIG_EDGE's output.
    SIG_AREA,         ///< Signed triangle area, per triangle, pre-cull. Checks max|E| == |area|.
    SIG_ZDIFF,        ///< |z_new - z_buffer| at the depth test, before the branch. Read BOTTOM-UP: sets depth FRACTIONAL bits.
    SIG_SCREEN,       ///< Vertex screen x,y after the viewport transform. Sizes the vertex register; a high bin means unclipped geometry.
    SIG_RECW,         ///< Vertex rec_w. Conditioning of the perspective divide.
    SIG_INVW,         ///< Interpolated inv_w_pixel. The divider's denominator, whose minimum is a scene property no format bounds.
    SIG_BARY,         ///< Normalised w0..w2. Verification: must follow the 2(1-w) density.
    SIG_COUNT         ///< Number of signals. Must stay last.
};

#if HISTOGRAM_STATS

/**
 * @brief Record one value into @p sig's histogram.
 * @param sig  Which signal.
 * @param v    The value. Sign is ignored; only magnitude sets width.
 */
void stats_tally(StatSignal sig, float v);

/**
 * @brief Write one CSV per signal.
 * @param prefix  Path prefix WITHOUT extension; each file is written to
 *                `<prefix>_<signal>.csv`. The directory must already exist.
 */
void stats_dump(const char* prefix);

    #define TALLY(sig, v)  stats_tally((sig), (v))
    #define STATS_DUMP(p)  stats_dump(p)

#else

    #define TALLY(sig, v)  ((void)sizeof(sig), (void)sizeof(v))
    #define STATS_DUMP(p)  ((void)sizeof(p))

#endif