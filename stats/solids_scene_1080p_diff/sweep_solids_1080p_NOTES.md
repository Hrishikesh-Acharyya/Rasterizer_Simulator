# Sub-pixel bit sweep — solids_scene, 1080p

Phase 3 of the fixed-point study. Third and final resolution; completes the set
with the 480p and 720p sweeps.

## Configuration

| | |
|---|---|
| model | `Media/Obj_files/solids_scene.obj` — 1,968 vertices, 3,636 triangles, 7 materials |
| resolution | 1920 × 1080 (2,073,600 px) |
| frames | 10 (frames 0–9, 3° rotation per frame) |
| camera | view matrix translate −2 on Z, 60° vertical FOV, near 0.1, far 100 |
| reference | `renderer.exe` with no argument → float `drawTriangle` |
| test | `renderer.exe <s>` → `drawTriangleFixed`, coverage in fixed point |
| comparison | `ppmdiff frames frames_s<N> 10` |

Only the coverage path is fixed-point: vertex snapping, the edge function, and
the inside test. Depth interpolation, the depth test, perspective correction and
colour blending remain float and byte-identical to the reference. Every
difference is a coverage decision.

## Files

| file | contents |
|---|---|
| `sweep_solids_1080p_summary.csv` | one row per `s` |
| `sweep_solids_1080p_histograms.csv` | `frac_bits, max_channel_difference, count` |
| `sweep_solids_1080p_perframe.csv` | `frac_bits, frame, differing, pct, max_channel, isolated` |
| `sweep_solids_ALL_RESOLUTIONS.csv` | all three resolutions in one table |

Integrity verified on load for all nine values of `s`: per-frame counts sum to the
total, isolated counts sum to the total, the histogram sums to the total, and the
histogram's largest occupied bucket equals the reported max channel difference.

## Result

| s | total | diff = 1 | **diff ≥ 2** | per frame | ratio/bit | max channel |
|---|---|---|---|---|---|---|
| 0 | 1,604,919 | 1,547,525 | 57,394 | 5,739.4 | — | 221 |
| 1 | 1,434,291 | 1,425,780 | 8,511 | 851.1 | 6.74 | 222 |
| 2 | 1,412,442 | 1,408,184 | 4,258 | 425.8 | **2.00** | 220 |
| 3 | 1,399,311 | 1,397,117 | 2,194 | 219.4 | **1.94** | 220 |
| 4 | 1,393,886 | 1,393,001 | 885 | 88.5 | **2.48** | 218 |
| 6 | 1,388,565 | 1,388,348 | 217 | 21.7 | **2.02** | 210 |
| 8 | 1,388,534 | 1,388,474 | 60 | 6.0 | **1.90** | 164 |
| 12 | 1,390,029 | 1,390,026 | 3 | 0.3 | 2.12 | 91 |
| 16 | 1,386,515 | 1,386,515 | **0** | **0.0** | — | **1** |

**Predicted `2^-s`; measured 2.00 / 1.94 / 2.48 / 2.02 / 1.90 / 2.12.**

### s = 16 converges exactly

Zero pixels differ by 2 or more, and the maximum channel difference across all
ten frames is 1. At 16 fractional bits the fixed-point rasterizer makes the
identical coverage decision to the float reference at every pixel of every frame.

This is the correctness proof for `drawTriangleFixed`: given enough precision it
reproduces the golden model exactly, so every difference at lower `s` is
attributable to quantisation alone. The same holds at 720p; 480p reaches 2 pixels
rather than 0, and Iron Man floors at 1,121 because of coincident geometry.

### s = 0 is off the curve by construction

`half = 0`, since 0.5 has no representation on a grid with zero fractional bits.
The sample point moves from the pixel **centre** to the pixel **corner** — a
systematic half-pixel shift on top of the snapping error. `s=0` has two error
sources where every other `s` has one, hence a ratio of 6.74 to `s=1` rather
than 2.

## The metric: diff ≥ 2, not the raw count

The difference histogram is two populations.

**diff = 1 is arithmetic-path noise.** 96.4% of the total at `s=0` and 100.0% at
`s=16`. It moves 154,753 → 138,652 per frame across the sweep — a 10% reduction —
while the coverage signal falls to zero. It arises because the fixed path reaches
the barycentric weights by a different route than the reference even when the
values agree to eight digits, and the truncating `uint8_t` cast turns a last-bit
difference into a whole level. The same population appears when the reference is
rebuilt at `-O0` instead of `-O2`.

**diff ≥ 2 is coverage.** A pixel switched between model and background, or
between two materials.

Reading the raw total would report 1,604,919 → 1,386,515, a 14% improvement, and
suggest fixed point barely converges. The coverage signal converges to **zero**.

---

# Three-resolution synthesis

## Coverage flips scale with linear resolution

diff ≥ 2 per frame:

| s | 480p | 720p | 1080p | 1080p/480p |
|---|---|---|---|---|
| 0 | 3,498.3 | 4,489.4 | 5,739.4 | 1.64 |
| 1 | 405.6 | 654.3 | 851.1 | 2.10 |
| 2 | 175.8 | 355.3 | 425.8 | 2.42 |
| 3 | 78.2 | 145.7 | 219.4 | 2.81 |
| 4 | 39.1 | 65.2 | 88.5 | 2.26 |
| 6 | 12.6 | 15.5 | 21.7 | 1.72 |
| 8 | 3.5 | 3.2 | 6.0 | 1.71 |

Linear scale ratio is 1920/854 = **2.25**, and the measured ratios bracket it
(mean ≈ 2.1 over s=1..8).

**Why:** coverage flips are proportional to (edge length × edge displacement).
Edge length in pixels scales linearly with resolution; the displacement
`2^-(s+1)` is measured in pixels and does not scale. So the count scales as the
linear resolution ratio.

This is the same structure as the phase-1 finding that
`W_edge ∝ 2·log2(resolution)`: coverage is a property of the sampling grid.

## As a fraction of the screen, the error *falls* with resolution

| s | 480p | 720p | 1080p |
|---|---|---|---|
| 0 | 0.853% | 0.487% | 0.277% |
| 4 | 0.0095% | 0.0071% | 0.0043% |
| 8 | 0.00085% | 0.00035% | 0.00029% |

Because the pixel count scales as the **square** of linear resolution while the
error scales linearly. Higher resolution is proportionally more forgiving of a
given `s`.

## The diff = 1 floor scales the other way

| | per frame | % of screen |
|---|---|---|
| 480p | 40,148 | **9.79%** |
| 720p | 62,085 | **6.74%** |
| 1080p | 138,652 | **6.69%** |

Proportionally **worse at lower resolution**, though it flattens between 720p and
1080p. The floor is caused by the truncating `uint8_t` cast crossing an integer
boundary, and how often that happens depends on the colour gradient per pixel. At
lower resolution a triangle spans fewer pixels for the same vertex-colour spread,
so the gradient per pixel is steeper and a given weight error crosses more
boundaries.

**Consequence for chunk 4:** the expected-difference budget is resolution
dependent, and in the counterintuitive direction. A lower-resolution RTL target
has a *larger* proportional noise floor to account for.

## Slope confirmed across every configuration

| configuration | ratios per bit, s=1→8 |
|---|---|
| solids 480p | 2.31, 2.25, 2.00, 1.76, 1.90 |
| solids 720p | 1.84, 2.44, 2.24, 2.05, 2.20 |
| solids 1080p | 2.00, 1.94, 2.48, 2.02, 1.90 |
| Iron Man 1080p | 1.75, 1.83, 1.99, 2.43, 2.68 |

Four configurations, three resolutions, two meshes, 20 measured ratios, all
bracketing 2. The `2^-s` relation is a property of vertex snapping and not of any
particular scene or resolution.

## Conclusion

    s = 4  →  W_edge = 19 + 2s = 27 bits

which fits `int32_t` with 5 bits to spare. At 1080p that leaves 88.5 pixels per
frame differing by 2 or more — 0.004% of the screen — in contiguous runs along
silhouette edges (`isolated` 2.8%). `s = 8` costs 8 more bits and forces a 64-bit
datapath for a 15× reduction in an already negligible count.

**Caveat: this metric cannot see temporal error.** Direct3D mandates `s = 8`
because at low `s` an edge snaps between quantised positions as geometry rotates,
which reads as crawling along silhouettes. A frame-versus-reference diff cannot
detect that; measuring it requires diffing consecutive frames of the *same* run at
different `s`, which is a different experiment and has not been done.
