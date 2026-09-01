# Sub-pixel bit sweep — solids_scene, 720p

Phase 3 of the fixed-point study. Companion to the 480p sweep; same model, same
frame count, same everything except `VIEWPORT_WIDTH`/`VIEWPORT_HEIGHT`.

## Configuration

| | |
|---|---|
| model | `Media/Obj_files/solids_scene.obj` — 1,968 vertices, 3,636 triangles, 7 materials |
| resolution | 1280 × 720 (921,600 px) |
| frames | 10 (frames 0–9, 3° rotation per frame) |
| camera | view matrix translate −2 on Z, 60° vertical FOV, near 0.1, far 100 |
| reference | `renderer.exe` with no argument → float `drawTriangle` |
| test | `renderer.exe <s>` → `drawTriangleFixed`, coverage in fixed point |
| comparison | `ppmdiff frames frames_s<N> 10` |

Only the coverage path is fixed-point: vertex snapping, the edge function, and
the inside test. Depth, perspective correction and colour blending remain float
and byte-identical to the reference. Every difference is a coverage decision.

## Files

| file | contents |
|---|---|
| `sweep_solids_720p_summary.csv` | one row per `s`, all derived metrics |
| `sweep_solids_720p_histograms.csv` | long form: `frac_bits, max_channel_difference, count` |
| `sweep_solids_720p_perframe.csv` | long form: `frac_bits, frame, differing, pct, max_channel, isolated` |

Integrity verified on load for all nine values of `s`: per-frame counts sum to the
reported total, isolated counts sum to the reported total, the difference
histogram sums to the total, and the histogram's largest occupied bucket equals
the reported max channel difference.

## The metric: diff ≥ 2

The difference histogram is two populations. Only one measures quantisation.

**diff = 1 is arithmetic-path noise.** 94.0% of the total at `s=0` and 100.0% at
`s=16`. It moves 69,901 → 62,085 per frame across the whole sweep — an 11%
reduction — while the coverage signal falls to zero. It arises because the fixed
path reaches the barycentric weights by a different route than the reference even
when the values agree to eight digits, and the truncating `uint8_t` cast turns a
last-bit difference into a whole level.

**diff ≥ 2 is coverage.** A pixel switched between model and background, or
between two materials.

## Result

| s | diff ≥ 2 | per frame | ratio per bit | max channel |
|---|---|---|---|---|
| 0 | 44,894 | 4,489.4 | — | 221 |
| 1 | 6,543 | 654.3 | 6.86 | 220 |
| 2 | 3,553 | 355.3 | **1.84** | 222 |
| 3 | 1,457 | 145.7 | **2.44** | 222 |
| 4 | 652 | 65.2 | **2.24** | 222 |
| 6 | 155 | 15.5 | **2.05** | 220 |
| 8 | 32 | 3.2 | **2.20** | 203 |
| 12 | 2 | 0.2 | 2.00 | 112 |
| 16 | **0** | **0.0** | — | **1** |

**Predicted `2^-s`; measured 1.84 / 2.44 / 2.24 / 2.05 / 2.20 / 2.00.** Six
consecutive ratios bracketing 2, over a range where the count falls by 2,000×.

### s = 16 converges exactly

**Zero pixels differ by 2 or more, and the maximum channel difference across all
ten frames is 1.**

At 16 fractional bits the fixed-point rasterizer makes the *identical* coverage
decision to the float reference at every pixel of every frame. The only residual
is the diff = 1 arithmetic noise floor, which is a property of the reference's own
float rounding rather than of quantisation.

This is the strongest available check that `drawTriangleFixed` is correct: given
enough precision it reproduces the golden model exactly, so any difference at
lower `s` is attributable to quantisation and to nothing else.

### s = 0 is off the curve by construction

`half = 0`, because 0.5 has no representation on a grid with zero fractional bits.
The sample point moves from the pixel **centre** to the pixel **corner** — a
systematic half-pixel shift on top of the snapping error. `s=0` therefore has two
error sources where every other `s` has one, and its ratio to `s=1` is 6.86 rather
than 2.

## Cross-resolution comparison (vs the 480p sweep)

### The coverage signal scales with linear resolution

| s | 480p /frame | 720p /frame | ratio |
|---|---|---|---|
| 0 | 3,498.3 | 4,489.4 | 1.28 |
| 1 | 405.6 | 654.3 | 1.61 |
| 2 | 175.8 | 355.3 | 2.02 |
| 3 | 78.2 | 145.7 | 1.86 |
| 4 | 39.1 | 65.2 | 1.67 |
| 6 | 12.6 | 15.5 | 1.23 |

Linear scale ratio is 1280/854 = **1.50**. Coverage flips are proportional to
(edge length × displacement), and edge length scales linearly with resolution
while the displacement `2^-(s+1)` is measured in pixels and does not. So the count
should scale as 1.50, and the observed 1.2–2.0 brackets that.

This is consistent with the phase-1 finding that `W_edge ∝ 2·log2(resolution)`:
coverage error is a property of the sampling grid.

### The diff = 1 floor scales the other way

| | per frame | % of screen |
|---|---|---|
| 480p | 40,148 | 9.8% |
| 720p | 62,085 | 6.7% |
| 1080p | ~135,000 | 6.5% |

Proportionally **worse at lower resolution**. The floor is caused by the
truncating `uint8_t` cast crossing an integer boundary, and how often that happens
depends on the colour gradient per pixel. At lower resolution a triangle spans
fewer pixels for the same vertex-colour spread, so the gradient per pixel is
steeper and a given weight error crosses more boundaries.

**Consequence for chunk 4:** the expected-difference budget is resolution
dependent, and not in the direction intuition suggests. A lower-resolution RTL
target has a *larger* proportional noise floor to account for.

## Conclusion

`s = 4`, giving

    W_edge = 19 + 2s = 27 bits

which fits `int32_t` with 5 bits to spare. At 720p that leaves 65 pixels per frame
differing by 2 or more — 0.007% of the screen. `s = 8` costs 8 more bits and
forces a 64-bit datapath for a 20× reduction in an already negligible count.

**Caveat: this metric cannot see temporal error.** Direct3D mandates `s = 8`
because at low `s` an edge snaps between quantised positions as geometry rotates,
which reads as crawling along silhouettes. A frame-versus-reference diff cannot
detect that; measuring it requires diffing consecutive frames of the same run at
different `s`, which is a different experiment and has not been done.
