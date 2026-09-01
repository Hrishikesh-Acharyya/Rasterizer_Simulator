# Sub-pixel bit sweep — torus_knot, 1080p

Fifth and final phase-3 run. Completes the set: three resolutions on
`solids_scene`, plus `torus_knot` and `IronMan` at 1080p.

## Configuration

| | |
|---|---|
| model | `Media/Obj_files/torus_knot.obj` — 6,400 vertices, 12,800 triangles, 8 materials |
| resolution | 1920 × 1080 (2,073,600 px) |
| frames | 10 (frames 0–9, 3° rotation per frame) |
| camera | view matrix translate −2 on Z, 60° vertical FOV, near 0.1, far 100 |
| reference | `renderer.exe` with no argument → float `drawTriangle` |
| test | `renderer.exe <s>` → `drawTriangleFixed`, coverage in fixed point |
| comparison | `ppmdiff frames frames_s<N> 10` |

Only the coverage path is fixed-point. Depth, perspective correction and colour
blending remain float and byte-identical to the reference.

## Files

| file | contents |
|---|---|
| `sweep_torus_1080p_summary.csv` | one row per `s` |
| `sweep_torus_1080p_histograms.csv` | `frac_bits, max_channel_difference, count` |
| `sweep_torus_1080p_perframe.csv` | `frac_bits, frame, differing, pct, max_channel, isolated` |
| `sweep_ALL_RUNS.csv` | all five runs in one table |

Integrity verified on load for all nine values of `s`.

## Result

| s | total | diff = 1 | **diff ≥ 2** | per frame | ratio/bit | isolated % | max ch |
|---|---|---|---|---|---|---|---|
| 0 | 1,691,663 | 1,541,786 | 149,877 | 14,987.7 | — | 3.26 | 207 |
| 1 | 525,200 | 517,063 | 8,137 | 813.7 | **18.4** | 22.00 | 207 |
| 2 | 355,655 | 352,310 | 3,345 | 334.5 | **2.43** | 26.58 | 207 |
| 3 | 271,284 | 269,707 | 1,577 | 157.7 | **2.12** | 24.64 | 204 |
| 4 | 225,096 | 224,391 | 705 | 70.5 | **2.24** | 18.57 | 204 |
| 6 | 190,402 | 190,211 | 191 | 19.1 | **1.92** | 8.61 | 204 |
| 8 | 181,012 | 180,963 | 49 | 4.9 | **1.97** | 4.64 | 145 |
| 12 | 178,839 | 178,832 | 7 | 0.7 | 1.63 | 3.25 | 110 |
| 16 | 178,135 | 178,134 | **1** | **0.1** | 1.63 | 3.19 | 60 |

`2^-s` holds from s=2 to s=8: **2.43, 2.12, 2.24, 1.92, 1.97**.

### s = 0 is catastrophic here — ratio 18.4, not 2

Far worse than any other run (solids 6.7–8.6, Iron Man 3.2). Two compounding
causes:

1. `half = 0`, so the sample point sits at the pixel **corner** rather than the
   centre — a systematic half-pixel shift on top of the snapping error. Common to
   all runs.
2. **The torus knot's triangles are small.** 12,800 triangles over roughly the
   same screen coverage as the solids scene's 3,636 gives ~10 px across per
   triangle versus ~25 px. A 0.5 px vertex displacement is 5% of a torus-knot
   triangle and 2% of a solids triangle.

This is the same relationship the isolated-triangle test showed: relative area
error from snapping scales as (displacement / triangle size), so small triangles
are hurt disproportionately. `s=0` is the regime where that dominates.

### Isolated fraction peaks at 26.6%

Far higher than solids (max 3.3%) and higher than Iron Man (15.1%). It rises from
3.3% at `s=0`, peaks at `s=2`, then falls back to 3.2%.

The peak is diagnostic of what the torus knot is: a self-crossing tube with eight
material bands, so most of its edge length is **interior** boundaries between
differently-coloured triangles rather than silhouette. A coverage flip there
produces a scattered pixel, not a run along an outline. At `s=0` the flips are so
numerous that even scattered ones have neighbours (hence the low 3.3%); by `s=6`
almost none remain.

## The diff = 1 floor scales inversely with tessellation density

At 1080p, `s=16`:

| model | triangles | floor px/frame |
|---|---|---|
| solids_scene | 3,636 | **138,652** |
| torus_knot | 12,800 | **17,813** |
| IronMan | 217,038 | **15,550** |

A 60× increase in triangle count gives a 9× *reduction* in the noise floor.

**Mechanism:** the floor is the truncating `uint8_t` cast crossing an integer
boundary, and how often that happens is proportional to the colour gradient per
pixel. Gouraud interpolates between three vertex colours across a triangle; on a
310 px triangle those colours differ noticeably, so a small weight error moves the
result across a level. On a 7 px triangle the three vertex normals are nearly
parallel, the three colours nearly equal, and the interpolated value barely
depends on the weights at all.

**Dense tessellation suppresses the noise floor.** Worth stating explicitly,
because it is the opposite of the intuition that more triangles means more error.

---

# Phase 3 complete — synthesis of all five runs

## Every configuration follows `2^-s`

| run | ratios per bit, s=1→8 |
|---|---|
| solids 480p | 2.31, 2.25, 2.00, 1.76, 1.90 |
| solids 720p | 1.84, 2.44, 2.24, 2.05, 2.20 |
| solids 1080p | 2.00, 1.94, 2.48, 2.02, 1.90 |
| torus_knot 1080p | 2.43, 2.12, 2.24, 1.92, 1.97 |
| IronMan 1080p | 1.75, 1.83, 1.99, 2.43, 2.68 |

**25 measured ratios, all bracketing 2**, across three resolutions and three
meshes whose triangle counts span 60×. The relation is a property of vertex
snapping, not of any scene.

Derivation: snapping displaces a vertex by at most `2^-(s+1)` px, so an edge line
moves by at most that distance perpendicular, and the band of pixels whose
coverage can flip is proportional to (edge length × displacement). Halving the
displacement halves the count.

## Convergence at s = 16

| run | diff ≥ 2 at s=16 | max channel |
|---|---|---|
| solids 720p | **0** | **1** |
| solids 1080p | **0** | **1** |
| solids 480p | 2 | 92 |
| torus_knot 1080p | 1 | 60 |
| IronMan 1080p | 1,121 | — |

Two configurations converge **exactly**: zero pixels differ by more than one
level, anywhere, in any frame. That is the correctness proof for
`drawTriangleFixed` — given enough precision it reproduces the golden model
exactly, so every difference at lower `s` is attributable to quantisation alone.

Iron Man does not converge, and phase 1 explains why: **169,640 exact-zero `zdiff`
values** — coincident geometry, unresolvable at any precision. The solids scene
had 4.

## What varies with the scene, and what does not

| quantity | scene-dependent? |
|---|---|
| `2^-s` slope | **no** — 25/25 measurements |
| exact convergence at high `s` | no, except where coincident geometry exists |
| absolute flip count | yes — scales with silhouette length |
| diff = 1 noise floor | yes — scales *inversely* with tessellation density |
| high-`s` residual | yes — set by coincident geometry |
| `s = 0` penalty | yes — worst on small triangles |

## Conclusion

    s = 4  →  W_edge = 19 + 2s = 27 bits

Fits `int32_t` with 5 bits to spare. At 1080p:

| model | diff ≥ 2 /frame at s=4 | % of screen |
|---|---|---|
| solids_scene | 88.5 | 0.0043% |
| torus_knot | 70.5 | 0.0034% |
| IronMan | 1,516.9 | 0.073% |

`s = 8` costs 8 more bits and forces a 64-bit datapath for a 10–20× reduction in
counts already below 0.1% of the screen.

**Caveat: this metric cannot see temporal error.** Direct3D mandates `s = 8`
because at low `s` an edge snaps between quantised positions as geometry rotates,
which reads as crawling along silhouettes. A frame-versus-reference diff cannot
detect that; measuring it requires diffing consecutive frames of the *same* run at
different `s`, which is a different experiment and has not been done.

## Chunk 4 acceptance criteria

The RTL rasterizer, diffed against this golden model at `s = 4`, 1080p, should
show approximately:

| model | diff = 1 /frame | diff ≥ 2 /frame | isolated % |
|---|---|---|---|
| solids_scene | ~139,000 | ~89 | ~3% |
| torus_knot | ~22,400 | ~71 | ~19% |
| IronMan | ~16,000 | ~1,500 | ~15% |

A diff within these bounds is quantisation. A diff above them is a bug. The
diff = 1 population is the reference's own float rounding and is irreducible.
