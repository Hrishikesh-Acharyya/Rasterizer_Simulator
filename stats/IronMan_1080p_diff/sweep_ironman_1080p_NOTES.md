# Sub-pixel bit sweep — IronMan, 1080p

Fifth phase-3 run, and the one that completes the set at a consistent 10 frames
across all five configurations.

## Configuration

| | |
|---|---|
| model | `Media/Obj_files/IronMan.obj` — 129,759 vertices, 217,038 triangles, 124 `usemtl` blocks / 11 defined materials |
| resolution | 1920 × 1080 (2,073,600 px) |
| frames | 10 (frames 0–9, 3° rotation per frame) |
| camera | view matrix translate −2 on Z, 60° vertical FOV, near 0.1, far 100 |
| reference | `renderer.exe` with no argument → float `drawTriangle` |
| comparison | `ppmdiff frames frames_s<N> 10` |

Six `usemtl` names have no match in the MTL and fall back to the neutral default,
which is expected and documented behaviour of the loader.

## Result

| s | total | diff = 1 | **diff ≥ 2** | per frame | ratio/bit | isolated % | max ch | mean ch |
|---|---|---|---|---|---|---|---|---|
| 0 | 871,992 | 544,373 | 327,619 | 32,761.9 | — | 2.15 | 213 | 7.575 |
| 1 | 462,728 | 368,893 | 93,835 | 9,383.5 | 3.49 | 7.63 | 215 | 4.002 |
| 2 | 360,768 | 308,247 | 52,521 | 5,252.1 | **1.79** | 10.59 | 213 | 2.667 |
| 3 | 296,632 | 268,015 | 28,617 | 2,861.7 | **1.84** | 13.17 | 211 | 1.881 |
| 4 | 254,703 | 240,909 | 13,794 | 1,379.4 | **2.08** | 14.36 | 206 | 1.341 |
| 6 | 209,118 | 206,890 | 2,228 | 222.8 | **2.49** | 14.29 | 193 | 0.893 |
| 8 | 192,747 | 192,464 | 283 | 28.3 | **2.81** | 12.49 | 174 | 0.778 |
| 12 | 186,099 | 186,073 | 26 | 2.6 | 1.82 | 11.28 | 115 | 0.739 |
| 16 | 185,972 | 185,954 | 18 | 1.8 | 1.10 | 11.30 | 95 | 0.738 |

`2^-s` holds from s=2 to s=8: **1.79, 1.84, 2.08, 2.49, 2.81**.

### A high-`s` residual that does not clear

diff ≥ 2 flattens at 26 (s=12) and 18 (s=16) — 1.8–2.6 px per frame — and stops
improving. Neither solids at 720p/1080p (which reach exactly 0) nor torus_knot
(which reaches 1) behaves this way.

Phase 1 gives the mechanism: Iron Man produced **169,640 exact-zero `zdiff`
values** over 120 frames, against 4 for the solids scene. Coincident geometry —
two surfaces at bit-identical NDC depth — is unresolvable at any coverage
precision, because the ambiguity is in the depth comparison rather than in the
edge function.

The `isolated` fraction confirms it: it rises from 2.15% at `s=0` to a 14.4% peak
at `s=4`, then settles near 11%. Coverage flips form contiguous runs along
silhouettes; depth ties are scattered single pixels. As the contiguous population
vanishes, the scattered one dominates.

**Note on the earlier 120-frame Iron Man sweep:** it reported a higher high-`s`
floor (9.3 px/frame at `s=16`). Frames 0–9 cover only 27° of rotation while the
120-frame run covers a full turn, and how many surfaces are coincident depends on
the viewing angle. The 10-frame figures here are the ones comparable to the other
four runs; the 120-frame figures are the better estimate of the full-rotation
average.

### Validity check

`mean channel difference` runs 7.575 → 0.738. Every valid run in this study falls
between 0.57 and 7.6. An earlier attempt at this sweep produced a constant 63.4
because the golden in `frames/` had not been re-rendered after switching models —
it still held torus_knot output. **`meanch` above ~10 means the two directories
were produced by different configurations, not that quantisation is large.**

---

# Phase 3 complete — five runs

## Every configuration follows `2^-s`

| run | triangles | ratios per bit, s=1→8 |
|---|---|---|
| solids 480p | 3,636 | 2.31, 2.25, 2.00, 1.76, 1.90 |
| solids 720p | 3,636 | 1.84, 2.44, 2.24, 2.05, 2.20 |
| solids 1080p | 3,636 | 2.00, 1.94, 2.48, 2.02, 1.90 |
| torus_knot 1080p | 12,800 | 2.43, 2.12, 2.24, 1.92, 1.97 |
| IronMan 1080p | 217,038 | 1.79, 1.84, 2.08, 2.49, 2.81 |

**25 measured ratios, all bracketing 2**, across three resolutions and three
meshes whose triangle counts span 60×.

Derivation: snapping displaces a vertex by at most `2^-(s+1)` px, so an edge line
moves by at most that distance perpendicular, and the band of pixels whose
coverage can flip is proportional to (edge length × displacement). Halving the
displacement halves the count.

## Convergence at s = 16

| run | diff ≥ 2 | max channel | interpretation |
|---|---|---|---|
| solids 720p | **0** | **1** | exact |
| solids 1080p | **0** | **1** | exact |
| solids 480p | 2 | 92 | near-exact |
| torus_knot 1080p | 1 | 60 | near-exact |
| IronMan 1080p | 18 | 95 | floor from coincident geometry |

Two configurations converge exactly: zero pixels differ by more than one level,
anywhere, in any frame. **That is the correctness proof for `drawTriangleFixed`**
— given enough precision it reproduces the golden model exactly, so every
difference at lower `s` is quantisation and nothing else.

## The diff = 1 noise floor scales inversely with tessellation density

At 1080p, `s=16`, per frame:

| model | triangles | floor |
|---|---|---|
| solids_scene | 3,636 | **138,652** |
| torus_knot | 12,800 | **17,813** |
| IronMan | 217,038 | **18,595** |

A 60× increase in triangle count gives a ~7× *reduction* in the noise floor, and
it saturates between 12,800 and 217,038 triangles.

**Mechanism:** the floor is the truncating `uint8_t` cast crossing an integer
boundary, and how often that happens is proportional to the colour gradient per
pixel. Gouraud interpolates between three vertex colours across a triangle; on a
310 px triangle those colours differ noticeably, so a small weight error moves the
result across a level. On a 7 px triangle the three vertex normals are nearly
parallel, the three colours nearly equal, and the interpolated value barely
depends on the weights.

The saturation is consistent: past a certain density the vertex colours are
already as close as they can get, and further subdivision buys nothing.

**Dense tessellation suppresses the noise floor.** Worth stating explicitly — it
is the opposite of the intuition that more triangles means more error.

## What varies with the scene and what does not

| quantity | scene-dependent? |
|---|---|
| `2^-s` slope | **no** — 25/25 measurements |
| exact convergence at high `s` | no, except where coincident geometry exists |
| absolute flip count | yes — scales with silhouette length |
| diff = 1 noise floor | yes — scales *inversely* with tessellation density |
| high-`s` residual | yes — set by coincident geometry |
| `s = 0` penalty | yes — worst on small triangles (torus knot ratio 18.4) |
| isolated fraction | yes — silhouette vs interior edge mix |

## Conclusion

    s = 4  →  W_edge = 19 + 2s = 27 bits

Fits `int32_t` with 5 bits to spare.

diff ≥ 2 per frame at `s = 4`:

| run | px/frame | % of screen |
|---|---|---|
| solids 480p | 39.1 | 0.0095% |
| solids 720p | 65.2 | 0.0071% |
| solids 1080p | 88.5 | 0.0043% |
| torus_knot 1080p | 70.5 | 0.0034% |
| IronMan 1080p | 1,379.4 | 0.066% |

`s = 8` costs 8 more bits and forces a 64-bit datapath for a 10–50× reduction in
counts already below 0.1% of the screen.

**Caveat: this metric cannot see temporal error.** Direct3D mandates `s = 8`
because at low `s` an edge snaps between quantised positions as geometry rotates,
which reads as crawling along silhouettes. A frame-versus-reference diff cannot
detect that; measuring it requires diffing consecutive frames of the *same* run at
different `s`, which is a different experiment and has not been done.

## Chunk 4 acceptance criteria

The RTL rasterizer, diffed against this golden model at `s = 4`, 1080p:

| model | diff = 1 /frame | diff ≥ 2 /frame | isolated % |
|---|---|---|---|
| solids_scene | ~139,300 | ~89 | ~2.8% |
| torus_knot | ~22,400 | ~71 | ~19% |
| IronMan | ~24,100 | ~1,379 | ~14% |

A diff within these bounds is quantisation. A diff above them is a bug. The
diff = 1 population is the reference's own float rounding and is irreducible by
adding fractional bits.

**Sanity check on any future comparison:** if `mean channel difference` exceeds
~10, the two directories were produced by different configurations. Re-render the
golden before drawing any conclusion.
