# Sub-pixel bit sweep — solids_scene, 480p

Phase 3 of the fixed-point study: how many fractional bits `s` the rasterizer's
vertex coordinates need.

## Configuration

| | |
|---|---|
| model | `Media/Obj_files/solids_scene.obj` — 1,968 vertices, 3,636 triangles, 7 materials |
| resolution | 854 × 480 (409,920 px) |
| frames | 10 (frames 0–9, 3° rotation per frame) |
| camera | view matrix translate −2 on Z, 60° vertical FOV, near 0.1, far 100 |
| reference | `renderer.exe` with no argument → float `drawTriangle` |
| test | `renderer.exe <s>` → `drawTriangleFixed`, coverage in fixed point |
| comparison | `ppmdiff frames frames_s<N> 10` |

Only the **coverage path** is fixed-point: vertex snapping, the edge function,
and the inside test. Depth interpolation, the depth test, perspective correction
and colour blending all remain float and are byte-identical to the reference
code. Any difference is therefore attributable to a coverage decision that
flipped, and to nothing else.

## Files

| file | contents |
|---|---|
| `sweep_solids_480p_summary.csv` | one row per `s`, all derived metrics |
| `sweep_solids_480p_histograms.csv` | long form: `frac_bits, max_channel_difference, count` |
| `sweep_solids_480p_perframe.csv` | long form: `frac_bits, frame, differing, pct, max_channel, isolated` |

Integrity verified on load: per-frame counts sum to the reported total, isolated
counts sum to the reported total, and the difference histogram sums to the total,
for all nine values of `s`.

## The metric: read diff ≥ 2, not the raw count

The difference histogram is **two populations**, and only one of them measures
quantisation.

**diff = 1 is arithmetic-path noise, not coverage.** It is 92.5% of the total at
`s=0` and 99.99% at `s=16`, and it barely moves across the sweep: 43,282 → 40,148
per frame, an 8% reduction while the coverage signal falls by four orders of
magnitude. It arises because the fixed path computes the barycentric weights by a
different route than the reference even when the two values agree to eight digits,
and the truncating `uint8_t` cast turns a last-bit difference into a whole level.
The same population appears when the reference is rebuilt at `-O0` instead of
`-O2` — same source, same scene, ~115,000 px/frame at 1080p.

**diff ≥ 2 is coverage.** A pixel switched between model and background, or
between two materials. The lumps in the histogram near 47–61, 70–92 and 101–111
are specific material-pair boundaries.

Reading the raw total would report 467,806 → 401,479, a 14% improvement, and
suggest fixed point barely converges. It converges by a factor of 17,000.

## Result

| s | diff ≥ 2 | per frame | ratio per bit |
|---|---|---|---|
| 0 | 34,983 | 3,498.3 | — |
| 1 | 4,056 | 405.6 | 8.63 |
| 2 | 1,758 | 175.8 | **2.31** |
| 3 | 782 | 78.2 | **2.25** |
| 4 | 391 | 39.1 | **2.00** |
| 6 | 126 | 12.6 | 1.76 |
| 8 | 35 | 3.5 | 1.90 |
| 12 | 1 | 0.1 | — |
| 16 | 2 | 0.2 | — |

**Predicted `2^-s`, measured 2.31 / 2.25 / 2.00 / 1.76 / 1.90.**

Derivation of the prediction: snapping displaces a vertex by at most `2^-(s+1)`
pixels, so an edge line moves by at most that distance perpendicular, and the band
of pixels whose coverage can flip is proportional to (edge length × displacement).
Halving the displacement halves the count.

### s = 0 is off the curve by construction

`half = 0`, because there is no representation of 0.5 on a grid with zero
fractional bits. The sample point therefore moves from the pixel **centre** to the
pixel **corner**, which is a systematic half-pixel shift on top of the snapping
error. `s=0` has two error sources where every other `s` has one, and its ratio to
`s=1` is 8.63 rather than 2.

### The floor is essentially zero

diff ≥ 2 reaches 1 pixel at `s=12` and 2 at `s=16` — over 10 frames, i.e. 0.1–0.2
per frame. This scene has almost no coincident geometry: the phase-1 `zdiff`
instrument recorded **4** exact-zero depth differences across 120 frames at 1080p.

By contrast Iron Man floors at 1,121 (9.3 per frame) and does not improve past
`s=12`, because it has **169,640** exact depth ties. Those are unresolvable at any
precision and are a property of the mesh, not of the arithmetic.

The two meshes bracket the design space from opposite ends.

## Secondary observations

**`isolated %` peaks at s=2 (0.74%) and falls to 0.13%.** Coverage flips form
contiguous runs along silhouette edges, so a low isolated fraction confirms the
signal is coverage rather than scattered depth ties. The non-monotonic shape is
the crossover between the two: at low `s` the flips are so numerous that even
isolated ones have neighbours; at high `s` almost nothing remains.

**`max_channel` falls 221 → 92** only at `s ≥ 12`. Below that a handful of large
differences persist, which is consistent with silhouette pixels switching between
model and background at every `s`.

**Per-frame counts vary 34,082 → 54,482 across the rotation** at `s=0`, a factor of
1.6, tracking how much silhouette faces the camera at each angle. The variation
shrinks with `s`; at `s=16` it is 34,082 → 43,653.

## Cross-resolution note

At 1080p the diff = 1 floor is ~135,000 px/frame, which is 24% of covered
fragments. At 480p it is ~40,150, which is ~36% of covered fragments. The floor is
proportionally **worse** at lower resolution: smaller triangles in pixel terms mean
a steeper colour gradient per pixel, so a given weight error crosses more integer
boundaries in the truncating cast.

## Conclusion

`s = 4` on this evidence, giving

    W_edge = 19 + 2s = 27 bits

which fits `int32_t` with 5 bits to spare. `s = 8` costs 8 more bits and forces a
64-bit datapath for a 11× reduction in a count that is already 39 px/frame — under
0.01% of the screen.

**Caveat: this metric cannot see temporal error.** Direct3D mandates `s = 8`
because at low `s` an edge snaps between quantised positions as geometry rotates,
which reads as crawling along silhouettes. A frame-versus-reference diff cannot
detect that; measuring it requires diffing *consecutive frames of the same run* at
different `s`, which is a different experiment and has not been done.
