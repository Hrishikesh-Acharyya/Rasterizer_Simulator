# Software Rasterizer

[![Docs](https://github.com/Hrishikesh-Acharyya/Rasterizer_Simulator/actions/workflows/pages.yml/badge.svg)](https://hrishikesh-acharyya.github.io/Rasterizer_Simulator/)

A 3D triangle rasterizer written from scratch in C++ — no OpenGL, no Vulkan, no
graphics library of any kind. The only drawing primitive in the whole program is
"write three bytes into an array". Everything above that — perspective
projection, backface culling, the depth test, perspective-correct barycentric
interpolation, Gouraud-shaded lighting, OBJ and MTL loading — is built by hand.

It is not trying to be fast. It is a **reference model**: an executable,
readable definition of what a rasterizer does, stage by stage, so that a
hardware implementation has something to be diffed against. That intent shows up
throughout — in a byte-layout assertion on the pixel struct, in POD types with
predictable flat layout, and in a material table shaped the way hardware wants
one.

**The result: the edge accumulator needs 27 bits — 19 integer plus 2 per
sub-pixel bit at `s = 4`.** The analytical bounds say 22 or 23 integer bits;
measuring 4.33 billion edge evaluations says 19. Three bits of a datapath, on
the widest-running unit in the pipeline, recovered by instrumenting the
reference instead of trusting the bound.

That is what the second rasterizer is for. It sits beside the float one with its
coverage path in **fixed point**, selectable at runtime; the float path is the
golden reference and the fixed path is diffed against it pixel for pixel. The
measurements are committed alongside — 45 exponent histograms and a sub-pixel
sweep across five configurations. Everything below is the evidence:
[the study](#the-fixed-point-study), or
[`Rasterizer_Study.pdf`](Rasterizer_Study.pdf) in full.

## Gallery

| | |
|---|---|
| <img src="Media/Gifs/ironman.gif" alt="Iron Man model rendered with per-material colour" width="380"><br>**Iron Man** — 129,759 vertices, **217,038 triangles**, 124 `usemtl` blocks naming 9 distinct materials. A downloaded model rather than a generated one, which is the point: it is the first mesh in the repo the loader did not produce itself. | <img src="Media/Gifs/human_model.gif" alt="Single-material human figure, smooth shaded" width="380"><br>**Human figure** — 24,459 faces, one material. Shares vertices throughout, so the averaged normals shade it smooth end to end. |
| <img src="Media/Gifs/solids_scene.gif" alt="Six coloured solids ringing a central torus, hard and soft edges in one render" width="380"><br>**Solids scene** — 3,636 triangles, 7 materials. Flat-faced solids keep their hard edges; the spheres and torus stay smooth. The central torus interpenetrates the ring by ~0.1 units, resolved per pixel by the depth buffer rather than by draw order. | <img src="Media/Gifs/torus_knot.gif" alt="A (2,3) torus knot in eight materials, crossings occluding each other" width="380"><br>**Torus knot** — 12,800 triangles, 8 materials. A (2,3) knot swept by a parallel-transport frame. The crossings occlude each other heavily, which is the depth test doing real work. |

Whether an edge is hard or soft is decided entirely by the mesh, not by a
renderer flag: the flat-faced solids carry per-face duplicated vertices, so
normal averaging has nothing to average across; the curved surfaces share
vertices and average smooth.

The Iron Man model is also the first one to exercise the loader's tolerance
rather than its parsing, and the exact counts are worth being careful about
because blocks and distinct names are not the same thing.

Its MTL has **11 `newmtl` blocks defining 8 distinct materials** — `red` is
defined three times and `14_-_Default` twice, and since the loader keys a
`std::map` by name, the last definition wins. Its OBJ has **124 `usemtl` blocks
naming 9 distinct materials**, one of which (`Iron_man_leg:red`) the MTL never
defines. That single unmatched name appears in six blocks, and the loader prints
its note once per block rather than once per name — so six notes, one missing
material. Each falls back to the neutral light grey default rather than
aborting, which is what the pale pieces are. The palette ends up with 9 entries:
the default at index 0, plus the 8 materials actually referenced and found.

A loader that only ever sees files it generated itself never learns whether it
can survive one it did not.

### Flat shading versus Gouraud

The same icosphere, the same 320 triangles, the same pose and the same light.
Only where the lighting is evaluated changed:

<img src="Media/png_files/flat_vs_gouraud.png" alt="Flat versus Gouraud shading on the same icosphere, with a magnified detail of each" width="700">

**Flat** evaluates `N·L` once per triangle from that triangle's own face normal,
and fills the whole triangle with the result. Every face is one constant colour,
so every shared edge is a step — visible in the detail as the triangle mesh
itself, drawn in light.

**Gouraud** evaluates `N·L` once per *vertex*, from a normal that is the
area-weighted average of every face meeting there, and lets the rasterizer
interpolate between the three results across the span. Adjacent triangles share
vertices, so they agree on the colour along their shared edge, and the step
disappears.

Three things worth taking from that:

- **The geometry did not change.** Still 320 flat triangles, still a faceted
  silhouette — look at the outline in either panel, which stays polygonal. Only
  the interior shading is smooth. Smooth shading cannot fix a coarse outline; it
  only stops you seeing the facets *within* the surface.
- **It is nearly free.** The rasterizer already interpolated colour across
  triangles for the barycentric fill. Gouraud reuses that machinery unchanged;
  the only added work is the normal-averaging pass, once at load, and three `N·L`
  evaluations per triangle instead of one. Nothing per pixel.
- **The mesh decides, not a flag.** A vertex shared by several faces averages
  across them and comes out smooth. A vertex duplicated per face has only its own
  face to average, so it stays hard — which is why the flat-faced solids in the
  scene above keep crisp edges while the spheres in the same render do not, with
  no per-object setting anywhere.

Phong shading is the next step along this axis: evaluate `N·L` per *fragment*
rather than per vertex, which fixes the remaining error on large triangles where
even the interpolated colour drifts from the true one. That one is not free — it
is a dot product and a normalise on every covered pixel.

In motion, with the earliest render for scale:

| | | |
|---|---|---|
| <img src="Media/Gifs/spinning_cube.gif" alt="Spinning cube, vertex colours interpolated across each face" width="240"><br>**Cube** — 12 triangles, colour interpolated across each face. | <img src="Media/Gifs/icosphere_lambertian_lighting.gif" alt="Flat-shaded icosphere showing triangle facets" width="240"><br>**Flat shaded** — one normal per triangle. | <img src="Media/Gifs/icosphere_gouraud_shading.gif" alt="Gouraud-shaded icosphere, facets smoothed away" width="240"><br>**Gouraud shaded** — normals averaged per vertex. |

Full-resolution versions of everything are in [`Media/Videos/`](Media/Videos),
including the before/after pairs for perspective correction and materials.

## Repository layout

```
src/                the renderer; headers sit beside their implementations
  main.cpp          scene setup and the per-frame render loop
  vectors.h         Vec3/Vec4 and pure vector arithmetic        (no dependencies)
  types.h           RGB and screenVertex                        (no dependencies)
  matrices.h        Mat4, transform builders, perspective divide
  model.h/cpp       OBJ + MTL loading, index resolution, bounds
  framebuffer.h/cpp colour + depth buffers, clears, PPM output
  raster.h/cpp      edge function, backface cull, drawTriangle
  raster_fixed.h/cpp  the fixed-point coverage path
  stats.h/cpp       exponent histograms behind a compile-time switch

tools/              standalone programs, each with its own main()
  ppmdiff.cpp       frame-sequence comparison

stats/              exponent histograms and sub-pixel sweep results (CSV)
Rasterizer_Study.pdf  the bit-width study written from that data

Makefile          build, render, encode video, docs, graphs
Doxyfile          Doxygen configuration
mainpage.dox      landing page for the generated documentation
LICENSE           MIT
.github/          Pages workflow, PR template, dependabot
.gitattributes    binary file markings and language statistics
Media/            Obj_files, Videos, Gifs, png_files, Graphs
```

Headers stay next to their implementations rather than in a separate `include/`.
That split exists so an install step can copy a public API somewhere; this
repository builds an executable and has no public API, so it would only mean
editing every include and navigating two directories to change one module.
`ppmdiff` is in `tools/` because a second `main()` is a real boundary — it must
stay out of the renderer's link step — rather than a filename one.

The include graph is kept a DAG on purpose — each `.cpp` can be compiled and
tested in isolation, which is what a testbench needs. An arrow reads "includes":

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="Media/Graphs/include_graph_dark.png">
  <img src="Media/Graphs/include_graph_light.png" alt="Module include graph" width="900">
</picture>

Every edge is a real `#include` of a project header, read off the sources rather
than drawn from memory. Three things in it are deliberate:

`vectors.h`, `types.h` and `stats.h` sit at the bottom and include nothing from
the project. `stats.h` is down there because the instrument depends on no
pipeline type, which is what lets any stage include it without creating a cycle.

`raster.h` deliberately does **not** include `framebuffer.h` — a caller needs to
know what a `screenVertex` is, not that a global framebuffer exists. `raster.cpp`
and `raster_fixed.cpp` both do, because the implementations write to those
globals (the dashed edges). Separating interface from binding is what would let
the rasterizer be pointed at a caller-supplied buffer instead of a global.

`raster_fixed.cpp` reaches into `raster.h` for one thing only: `bounding_box`.
Sharing it rather than copying it is what keeps a measured difference
attributable to coverage rather than to box arithmetic.

`tools/ppmdiff.cpp` is absent from the graph because it includes no project
header at all — its only contract with the renderer is the PPM files on disk,
which is what makes it a separate program rather than another module.

---

## How it works

Everything runs on the CPU, single threaded, one triangle at a time.

```
OBJ + MTL
   |  loadOBJ()               parse v/f/mtllib/usemtl, resolve indices,
   |                          fan-triangulate, emit a material index per triangle
   v
model space
   |  vertex normal pass      accumulate unnormalised face normals per vertex
   |  normalise matrix        centre on origin, scale largest axis to 2 units
   |  rotation X * Y * Z      3 degrees per frame, 120 frames = one full turn
   v
world space                   <- N.L evaluated here, per vertex
   |  view matrix             translate -2 on Z; camera at +2 looking down -Z
   |  perspective matrix      60 deg vertical FOV, near 0.1, far 100
   v
clip space
   |  perspectiveTransform()  one reciprocal: x,y,z / w, keeping 1/w
   v
NDC  [-1, 1]
   |  viewport map            -> 1920 x 1080 pixels, Y flipped
   |  snap to 2^-s grid       ONLY when a frac_bits argument was given;
   |                          fills the integer xi/yi beside the floats
   v
screen space
   |
   |  one branch per triangle on g_frac_bits, set once from argv:
   |
   +--> drawTriangle()        FLOAT, the golden reference
   |      backface cull         one signed-area test, first thing in the function
   |      bounding box          clamped to the viewport
   |      3 edge functions      per pixel, at the pixel centre
   |      depth test            interpolated NDC z against the z-buffer
   |      perspective-correct   colour, one reciprocal per surviving fragment
   |
   +--> drawTriangleFixed()   FIXED-POINT COVERAGE, for the study
          backface cull         same rule, but an INTEGER sign test -- no
                                epsilon, no tie-break ambiguity
          bounding box          the SAME function, on the unsnapped floats
          3 edge functions      int64, on the snapped xi/yi, at 2^-s precision
          everything after      copied verbatim from drawTriangle, still float
   v
framebuffer -> writeFramebufferToPPM()
```

Only the *coverage* decision differs between the two paths. Depth, perspective
correction and colour are float in both, so any pixel that disagrees is a
coverage decision that flipped and nothing else — which is what makes the
difference attributable.

### 1. Getting geometry in

`loadOBJ` reads `v`, `f`, `mtllib` and `usemtl`. Texture coordinates, file
normals, groups and smoothing are skipped, which is why the renderer synthesises
its own normals.

A face token can be `5`, `5/2`, `5//3` or `5/2/3`, and only the part before the
first slash is the position index. Indices are then fully resolved at the loader:
positive ones offset by one, **negative ones resolved against the running vertex
count**, zero rejected, and the result range-checked. Negative indices are what
make OBJ files concatenable — appending one model to another leaves its
backwards references pointing at its own vertices — and they are also a memory
hazard, since `std::stoi("-1") - 1` is `-2` and `vector::operator[]` does not
bounds check.

Everything downstream receives zero-based indices guaranteed to be in range, and
never learns the other forms exist.

A malformed face discards the **whole face**, not the offending vertex. Dropping
one vertex silently reshapes the polygon — a quad becomes a triangle and
fan-triangulates into geometry that looks deliberate. A missing face is a visible
hole; a reshaped face is not.

Polygons of any size are fan-triangulated:

```cpp
out_indices.push_back(face[0]);
out_indices.push_back(face[i]);
out_indices.push_back(face[i + 1]);
```

Correct only for **convex** faces; on a concave polygon some fan triangles fall
outside the outline, and nothing in the loader checks for it.

The bundled models are triangles and quads, except the solids scene, whose
n-gon caps run up to 24 vertices. The generated models are convex by
construction; the two downloaded ones (Iron Man, the human figure) are quads
throughout and have not been checked, so on those this is an assumption that
happens to hold rather than a guarantee. Iron Man's 149,827 faces
fan-triangulate to 217,038 triangles, which is 82,616 triangles and 67,211
quads.

### 2. Materials

Colour was a placeholder for a long time — each vertex tinted by its position
within the bounding box, which is not an OBJ concept at all. It now comes from
the MTL file.

The shape of the feature is the interesting part. **Material is not per-face
data.** No `f` line carries a colour. `usemtl` is a *state write* that selects
the material in force for every subsequent face until the next one, so binding is
decided by line order in the file. Those boundaries are draw-call boundaries —
which is exactly why the index sits beside the triangle rather than inside the
vertex:

```cpp
std::vector<int>  tri_materials;   // one index per TRIANGLE
std::vector<RGB>  materials;       // palette; entry 0 is the default
```

A GPU binds a material as state and issues a draw; one `usemtl` block can cover
thousands of triangles. The palette accumulates only materials actually
referenced, in first-use order, deduplicated by name — a fifty-entry MTL used
three times yields four slots including the default. Small and dense, the shape a
hardware material table wants.

Entry 0 is unconditionally a neutral default and is what is in force before any
`usemtl`, so a model with no materials needs no special case anywhere downstream.

Only `Kd` is read; `Ka`, `Ks`, `Ns` and the `map_*` lines are skipped because the
shading model is Lambertian diffuse with a constant ambient and there is nowhere
to put them.

The material index is pushed **inside** the fan-triangulation loop. Pushing once
per face desynchronises the two arrays from the first quad onward, and
mis-coloured geometry reads as a rendering bug rather than a parsing one. `main`
asserts the invariant:

```cpp
assert(tri_materials.size() * 3 == obj_indices.size());
```

### 3. Placing the model

Models arrive at arbitrary scales and offsets, so `normalizationPass` measures
the axis-aligned bounding box and returns it as a named struct:

```cpp
boundingBox box = normalizationPass(obj_verts);
```

```cpp
float centre_x = (box.min.x + box.max.x) / 2.0f;
```

The render loop centres the model on the origin and scales its largest axis to
span 2 units:

```cpp
Mat4 normalise = multiply_matrices(scaleM, centreM);
```

Note the order. With column vectors, `multiply_matrices(a, b)` applied to `v`
gives `a*(b*v)` — **`b` acts first**. So "centre, then scale" is written
`multiply(scale, centre)`, which reads backwards from how you say it. Scaling
before centring would scale the offset too and throw the model off screen. This
order-reversal is the single most common source of transform bugs.

### 4. Vertex normals

The loader discards any normals in the file, so they are computed from the
geometry. For each triangle, the face normal is added to all three of its
vertices; a vertex shared by N faces accumulates N contributions:

```cpp
Vec3 edge1 = subtract_Vec3(B, A);
Vec3 edge2 = subtract_Vec3(C, A);
Vec3 faceNormal = cross_Vec3(edge1, edge2);
```

The subtle part: face normals are accumulated **unnormalised**. A cross product's
magnitude is twice the triangle's area, so each face automatically contributes in
proportion to its size — the correct area-weighted average. Normalising inside
this loop would weight a sliver the same as a large quad. Normalisation happens
once, after every contribution is summed.

This is also why hard and soft edges are a property of the mesh: a vertex
duplicated per face has only one contribution to average.

### 5. The transform chain

Transforms are 4×4 rather than 3×3 because translation is not a linear operation
on 3D points. Lifting to homogeneous coordinates makes it linear in 4D, which
lets the whole model → world → view → clip chain collapse into one matrix:

```cpp
Mat4 mvp = multiply_matrices(perspectiveMatrix,
               multiply_matrices(view, world_space));
```

Composed once per frame rather than once per vertex, three transforms per vertex
become one. That collapse is the entire reason a GPU has a vertex stage: one 4×4
multiply per vertex, uniform cost, no branches.

The `w` component distinguishes positions (`w=1`, affected by translation) from
directions (`w=0`, immune). That is why normals are transformed with `w = 0`.

#### The perspective matrix

A point at eye-space depth `-z` projects onto a screen at distance `d` by similar
triangles: `x_screen = d*x / -z`. The division by `z` is the entire content of
perspective, and it is **not linear**, so no 4×4 matrix can perform it. The trick
is to have the matrix copy `-z` into the output `w` and let the division happen
afterwards:

```cpp
perspectiveMatrix.m[0][0] = t / aspectRatio;
perspectiveMatrix.m[1][1] = t;
perspectiveMatrix.m[2][2] = -(farPlane + nearPlane) / (farPlane - nearPlane);
perspectiveMatrix.m[2][3] = -(2.0f * farPlane * nearPlane) / (farPlane - nearPlane);
perspectiveMatrix.m[3][2] = -1.0f;
```

`t = 1/tan(fov/2)` is the distance to a screen of half-height 1. Only 6 of the 16
elements are meaningful, which is why the builder zeroes the matrix first —
`Mat4 m;` is 16 floats of stack garbage.

The `m[2][2]`/`m[2][3]` pair maps `[near, far]` onto NDC `[-1, 1]` *after* the
divide. Because that mapping is in `1/z` rather than `z`, depth precision is
heavily front-loaded. That is z-fighting's root cause, and it is directly
relevant to the hardware version: **a fixed-point depth buffer has to budget its
bits around this non-uniformity.**

#### The divide, and what it keeps

The reciprocal is computed once per vertex and *kept*, because the rasterizer
needs it later:

```cpp
float x,y,z,rec_w;
```

`rec_w` is the reciprocal of the clip-space `w` that produced this vertex's NDC
position — the one piece of pre-divide information the fragment stage needs.
Stored as the reciprocal because the divide already computes it; storing `w`
would mean recomputing the reciprocal once per *fragment* instead of once per
vertex.

#### Viewport transform

Y is flipped because NDC has `+Y` up while framebuffer row 0 is the top of the
screen. Coordinates stay **floats** — rounding vertices to the integer grid would
quantise geometry and throw away sub-pixel accuracy. Only sample points sit on
the integer grid, which is why sampling happens at pixel centres.

### 6. Backface culling

One test per triangle, before the bounding box and before any per-pixel work:

```cpp
if (area >= 0.0f)
    return;
```

`area` is the signed screen-space area from the same edge function the rasterizer
uses per pixel. Its sign is the triangle's winding as seen from the camera: a
triangle rotating away passes through zero, edge-on, and emerges with the sign
reversed. The projection has already done the work; this only reads the result.

**Which sign is front-facing was measured, not derived.** The pipeline contains
two candidate reversals — the viewport Y flip and the sign of `w` — and reasoning
through them gave the wrong answer. The cube at zero rotation settles it: its
single visible face is the only negative pair, and the magnitudes check out, with
the front face projecting `(5/3)² = 2.778` times the area of the back one at the
camera distance of 4 in use at the time, against a measured 2.778. (The camera
sits at +2 today; that check was taken on the cube before the distance changed,
and the ratio is a property of the two depths, not of the current scene.)

Using `>=` rather than `>` also drops degenerate zero-area triangles, which would
otherwise divide by zero when normalising the barycentric weights.

This is the cheapest work-elimination in the pipeline: one comparison at the
narrow end removes an entire triangle's worth of work at the wide end. Real
hardware culls in fixed-function logic between the vertex stage and the
rasterizer for exactly this reason.

Finding this also fixed a rendering bug. `torus.obj` turned out to be wound
inward on all 800 faces — uniformly wrong rather than inconsistent. Face normals
are built from the same cross product, so they pointed inward, `N·L` carried the
wrong sign, and every genuinely lit region clamped to the ambient floor. The
torus had been rendering darker than the icosphere and it had been blamed on the
placeholder colours.

### 7. Coverage

The heart of the whole program is one function evaluated three times per pixel:

```cpp
float edge_function(float ax, float ay, float bx, float by, float cx, float cy) {
    return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
}
```

Two multiplies, five adds. No division, no branch, no memory access. It returns
twice the signed area of triangle `ABC`, and its sign says which side of the
directed edge `AB` the point `C` lies on.

`drawTriangle` walks the triangle's screen-space bounding box and evaluates it at
every pixel centre:

```cpp
float w0 = edge_function(B.x, B.y, C.x, C.y, x + 0.5f, y + 0.5f);
float w1 = edge_function(C.x, C.y, A.x, A.y, x + 0.5f, y + 0.5f);
float w2 = edge_function(A.x, A.y, B.x, B.y, x + 0.5f, y + 0.5f);

bool inside = (w0 >= 0 && w1 >= 0 && w2 >= 0) ||
              (w0 <= 0 && w1 <= 0 && w2 <= 0);
```

The cost is identical for every pixel regardless of geometry, and no pixel's
result depends on any other's. That uniformity is precisely what makes
rasterization parallelisable.

The same three numbers then become barycentric weights:

```cpp
w0 = w0 / area;
w1 = w1 / area;
w2 = w2 / area;
```

Each `w` was twice the area of the sub-triangle opposite its vertex; dividing by
the total turns them into weights summing to 1. **Normalising before interpolating
depth matters**: with raw weights, `z` is scaled by the triangle's area, making
the depth test area-dependent. It looks correct on simple scenes by coincidence
and fails on interpenetrating geometry — which the solids scene has.

The bounding box is clamped to the viewport, and that clamp is load-bearing —
`drawTriangle`'s writes are unchecked, so an off-screen vertex without it indexes
past the end of the vector.

### 8. Depth

```cpp
float z = w0 * A.z + w1 * B.z + w2 * C.z;
```

The buffer is cleared to `+infinity` so the first fragment at any pixel always
passes. Interpolating NDC `z` linearly in screen space is **exact** — NDC depth
is `A + B/z_eye`, already a function of `1/z`, which is the same reason a hardware
depth buffer stores this quantity rather than view-space distance. Correcting it
would make it wrong.

The bandwidth shape is worth noticing: **every fragment costs a depth read, and
only survivors cost a depth write plus a colour write.** Consecutive pixels share
no data, so this is streaming traffic with no reuse. At 1080p that is millions of
accesses per frame, and it is what makes a GPU a memory-bandwidth problem rather
than an arithmetic one — the reason real hardware spends transistors on
hierarchical-Z and depth compression rather than on more ALUs.

### 9. Perspective-correct interpolation

Barycentric weights are computed in screen space, and a linear function on a
surface does not project to a linear function on the screen: perspective
compresses a triangle's far half into fewer pixels than its near half, so walking
pixels at a uniform rate walks the *surface* at a non-uniform one. Interpolating
colour with the screen-space weights directly places each vertex's contribution
at the wrong point along the face.

What *does* vary linearly in screen space is `f/w` and `1/w`. So the correct value
is a weighted average of `f/w` divided by a weighted average of `1/w`. Folding the
denominator into the weights gives:

```cpp
float inv_w_pixel = w0 * A.rec_w + w1 * B.rec_w + w2 * C.rec_w;
```

```cpp
float c0 = w0 * A.rec_w * inv_w_pixel_recip;
float c1 = w1 * B.rec_w * inv_w_pixel_recip;
float c2 = w2 * C.rec_w * inv_w_pixel_recip;
```

`c0..c2` still sum to 1 but lean toward the nearer vertex in proportion to the
depth ratio. The cost is one extra reciprocal **per fragment** — in hardware, a
divider on the critical path of every covered pixel rather than one per vertex.
That cost is why the affine shortcut survived as long as it did in real hardware.

The size of the error scales with the depth a single triangle spans. On a model
built to expose it — three crossed elongated boxes, 36 large triangles each
spanning the full model depth — the worst per-channel difference is 126 of 255
and every one of the 120 frames differs. On the torus at 1,600 triangles it is
almost nothing.

### 10. Shading

Lighting is evaluated per **vertex** (Gouraud), and the rasterizer interpolates
the results:

```cpp
const RGB base = materials[tri_materials[i / 3]];
```

```cpp
float intensityA = 0.4f + 0.6f * std::max(0.0f, dot_Vec3(world_normals[ia], lightDir));
```

For two unit vectors the dot product is exactly `cos(theta)`, which is the whole
basis of Lambertian diffuse shading: brightness falls off as the cosine of the
angle between surface normal and light, because a beam of fixed cross-section
spreads over a larger area as the surface tilts away. `max(0, N·L)` clamps
surfaces facing away rather than letting them go negative, and the `0.4` ambient
floor keeps them visible instead of black.

`screenVertex.color` holds the **shaded result** — material diffuse times that
vertex's intensity — not a base colour. The base is per-triangle and only the
intensity varies per vertex, so the three colours across one triangle share a hue
and differ in brightness.

Phong shading would move the `N·L` into the per-pixel loop: far more accurate on
large triangles, and several orders of magnitude more expensive.

### 11. Getting pixels out

```cpp
static_assert(sizeof(RGB) == 3, "RGB must be tightly packed");
```

`writeFramebufferToPPM` `fwrite`s the entire pixel vector as raw bytes without
ever naming `.r`/`.g`/`.b`, so `RGB`'s byte layout is a contract with the file
format, not just a container. Three `uint8_t` cannot be padded in practice, but
nothing in the language guarantees it — and if it ever changed, every pixel after
the first would shift and produce diagonal garbage rather than an error. The
assertion turns silent corruption into a compile failure.

## Where the work is

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="Media/Graphs/work_amplification_dark.png">
  <img src="Media/Graphs/work_amplification_light.png" alt="Work per stage, per frame" width="600">
</picture>

Every count is **measured, not estimated** — the vertex, triangle, bounding-box
and covered-fragment figures are histogram totals from [`stats/`](stats), divided
by their tallies per event and by the 120 frames of the run. The vertex and
triangle counts recovered this way match the loader's own report exactly, which
is what makes the other two trustworthy. Node width widens down the funnel as a
visual cue only; it is set by hand and is not a linear scale.

Two things are worth reading off it. **The inside test throws away 87.6% of what
the edge functions evaluate** — nearly six million bounding-box pixels tested per
frame to shade 734,421 — which is why hierarchical coverage rejection is the
first optimisation real hardware reaches for.

And note the narrowing at the cull: the one place the funnel runs backwards, a
per-triangle sign test that removes roughly half the geometry before any
per-pixel work happens. Cheap work placed upstream of expensive work is the whole
trade. (That "roughly half" is the one figure here not measured — `ilogb`
discards sign, so the histograms cannot count negative areas.)

## What it costs

**The pipeline runs at ~75 ms per frame** — 217,038 triangles transformed,
culled, shaded and rasterized into 1920×1080, single threaded, `-O2`.

A full 120-frame run takes ~13.3 s of wall clock, of which ~9.0 s is user time;
the other ~4.3 s is the kernel writing 746 MB of uncompressed PPM to disk. No
renderer dumps every frame to disk uncompressed — that is this harness, not the
pipeline, and it should be subtracted before drawing any conclusion about where
the work goes.

| Model | Triangles | user | per frame |
|---|---|---|---|
| solids scene | 3,636 | ~2.8 s | ~23 ms |
| Iron Man | 217,038 | ~9.0 s | **~75 ms** |

Sixty times the geometry for three times the time. The vertex stage scales with
the model; the fragment stage scales with *coverage*, and both models fill a
similar fraction of the same 1920×1080 frame. Past a certain triangle count the
pipeline stops being geometry-bound and the cost sits at the wide end regardless
— which is the whole reason the wide end is what gets built into fixed-function
hardware.

## The fixed-point study

The renderer exists to be a golden reference for hardware, and hardware needs
numbers: how many bits does each datapath need? The full write-up is
[`Rasterizer_Study.pdf`](Rasterizer_Study.pdf) — 19 pages, with the per-run notes
and every CSV under [`stats/`](stats). The short version:

**How wide? — exponent histograms.** `stats.h` records `std::ilogb(v)` for nine
signals in the float reference. A value binned at exponent `e` needs
`W = e + 2` bits, so the histogram *is* the bit-width distribution. Reading the
cumulative sum from the top turns one worst case into a design space — solids
scene, 715,426,158 edge evaluations:

| accumulator width | evaluations it overflows | share |
|---|---|---|
| **19 bits** | **0** | **0.00%** |
| 18 bits | 7,182,200 | 1.00% |
| 17 bits | 41,644,534 | 5.82% |

**How many sub-pixel bits? — the sweep.** `drawTriangleFixed` snaps vertices to a
`2^-s` grid; sweeping `s` and diffing against the float reference gives the
error curve. The metric matters more than the count: `diff = 1` is
arithmetic-path noise from the truncating `uint8_t` cast and barely moves,
`diff ≥ 2` is a coverage decision that flipped. Reading the raw total says fixed
point improves 14% and hardly converges; reading the coverage column says it
converges. Each extra bit halves it — 25 measured ratios across five
configurations (three meshes, three resolutions) all bracket 2.0.

(`s = 0` is off that curve everywhere, 3.5× to 18.4×, by construction: with no
fractional bits there is no 0.5, so the sample lands on the pixel *corner* — a
half-pixel shift on top of the snapping error.)

At `s = 16`, **two of the five configurations converge exactly** — zero pixels
differing by more than one level, anywhere, in any frame. That is the
correctness proof for `drawTriangleFixed`: given enough precision it reproduces
the golden model exactly, so every difference at lower `s` is quantisation. The
other three floor on coincident geometry rather than precision (torus knot 1
pixel, solids 480p 2, Iron Man 18).

### The answer

```
s = 4  ->  W_edge = 19 + 2s = 27 bits
```

Nineteen integer bits *measured*, against 22 from the geometric bound and 23
from Q propagation — both correct, both loose, because they assume a worst case
the geometry never reaches. 27 fits `int32_t` with five to spare; depth needs
zero integer bits and 16 fractional. At `s = 4`, 88.5 pixels per frame differ by
2 or more at 1080p — 0.004% of the screen, in runs along silhouettes.

Two findings the analysis could not have given:

- **Required width is uncorrelated with triangle count.** One mesh has 17× the
  triangles of another and needs two more bits, because width tracks the
  *largest* triangle, not the mean.
- **The `diff = 1` noise floor falls as tessellation rises, then saturates** —
  and proportionally it is *worse* at lower resolution (9.79% of the screen at
  480p against 6.69% at 1080p). A lower-resolution hardware target has a larger
  proportional noise floor to budget for, which is the counterintuitive
  direction.

**The metric cannot see temporal error.** A frame-versus-reference diff cannot
detect edge crawl — an edge snapping between quantised positions as geometry
rotates. Direct3D mandates `s = 8` for that reason where this static metric is
content with 4. Measuring it means diffing consecutive frames of the same run,
which is a different experiment and has not been done. `s = 4` answers the
question that was asked, not every question.

## Build and run

Needs a C++17 compiler and nothing else.

The Makefile targets **both** Windows and Linux/macOS from one file. Windows sets
`$(OS)` to `Windows_NT`, and the Makefile branches on it: that branch keeps
`SHELL := cmd.exe`, `.SHELLFLAGS := /C`, `del`/`if not exist`, and `%%` for
ffmpeg's frame pattern; the other branch uses the POSIX shell, `rm -f`, `mkdir -p`
and `%`. Neither branch is a fallback for the other: the Windows one is the
original, and the POSIX one is what every command in this README assumes.

```bash
make            # build BOTH programs: renderer and ppmdiff
make run        # build, clear stale frames, render 120 frames into frames/
make video      # run, then encode an mp4        (needs ffmpeg)
make docs       # doxygen HTML into docs/html/   (needs doxygen + graphviz)
make graphs     # redraw the README graphs       (needs graphviz)
make clean
```

Plain `make` builds both programs deliberately. `ppmdiff` is how the renderer's
output gets checked, and letting it drift out of date relative to the renderer
it is measuring is exactly the kind of thing that wastes an afternoon.

### Running the renderer

```bash
./renderer          # float golden reference   -> frames/
./renderer 4        # fixed-point coverage, s=4 -> frames_s4/
./renderer 99       # rejected: frac_bits must be 0..16
```

The argument is `s`, the number of sub-pixel fractional bits. With no argument
the float path runs and writes to `frames/`; with one, the fixed-point coverage
path runs and writes to `frames_s<s>/`, so two configurations can never
overwrite each other's output. Both are echoed at startup:

```
model=Media/Obj_files/IronMan.obj  1920x1080  120 frames  frac_bits=4  output=frames_s4/
```

That banner exists because model, resolution and frame count are compile-time
constants — nothing else distinguishes one output directory from another, and
comparing directories rendered from different configurations produces numbers
that look plausible and mean nothing.

The upper bound of 16 is not arbitrary. The edge accumulator needs `19 + 2s`
bits, so `s = 16` needs 51 and fits `int64_t` with room; `s = 22` would need 63
and overflow silently.

### Reproducing a sweep

```bash
mkdir -p frames && ./renderer            # the reference
for s in 0 1 2 3 4 6 8 12 16; do
    mkdir -p frames_s$s && ./renderer $s
done
./ppmdiff frames frames_s4 10            # compare 10 frames
```

`ppmdiff` reports differing pixels, maximum and mean channel difference, an
isolated-pixel count and a difference histogram. The isolated count is what
separates the two populations: scattered single pixels with a large channel
difference are depth tie-breaking on near-coplanar geometry, while contiguous
runs along triangle edges are a coverage shift. A pixel count alone cannot tell
them apart.

Regenerating the phase-1 histograms instead means setting `HISTOGRAM_STATS` to
`1` in `src/stats.h`, rebuilding, and rendering with no argument; the CSVs land
under the `STATS_PREFIX` set in `src/main.cpp`, and that prefix has to be
changed to match whenever the model or resolution changes, or the data is filed
under the wrong configuration.

Without make:

```bash
g++ -std=c++17 -Wall -Wextra -O2 -Isrc \
    src/main.cpp src/framebuffer.cpp src/raster.cpp src/raster_fixed.cpp \
    src/model.cpp src/stats.cpp -o renderer
mkdir frames && ./renderer
```

`-Isrc` is what lets every `#include "raster.h"` stay a bare filename. Leave a
translation unit out and the link fails; leave `-Isrc` out and the compile does.

**Paths resolve from the working directory**, so run from the repository root —
`src/main.cpp` loads `Media/Obj_files/IronMan.obj`. Change that line to swap models;
`Human_Model.obj`, `solids_scene.obj`, `torus_knot.obj`, `torus.obj`,
`icosphere.obj`, `jack.obj` and `test.obj` are also there. An MTL referenced by
`mtllib` resolves against the OBJ's own directory, not the working directory.

**`frames/` gets large.** At 1920×1080 one P6 PPM is 6,220,817 bytes and a
120-frame run writes 746,498,040 — 746 MB, or 712 MiB. It is gitignored, and
`make run` clears it first: a leftover frame the new run does not overwrite would
silently appear in the video and look like a rendering bug.

### Docs

The generated Doxygen HTML is published to
**[hrishikesh-acharyya.github.io/Rasterizer_Simulator](https://hrishikesh-acharyya.github.io/Rasterizer_Simulator/)**
on every push to `main` that touches a source file
([`pages.yml`](.github/workflows/pages.yml)). It carries the design notes from
the headers plus automatically generated include, collaboration and call graphs.

That is the only automation in the repository. There is no build or test
workflow: this is a single-author project where a build failure surfaces on the
next compile.

If you want the checks a CI job would have run, they are one command each:

```bash
make CXXFLAGS="-std=c++17 -Wall -Wextra -Werror -O2"          # warnings are errors
make CXX=clang++ CXXFLAGS="-std=c++17 -Wall -Wextra -Werror -O2"
make CXXFLAGS="-std=c++17 -g -O1 -fsanitize=address,undefined" && make run
```

The sanitizer pass is the one worth running after touching the loader or the
rasterizer. Two hazards are structural here: framebuffer writes are unchecked
behind a bounding box that must be clamped, and OBJ indices are read from a file
and used directly as array subscripts. Both are clean across the full
217,038-triangle Iron Man model — the first mesh here the loader did not generate
itself.

## Known gaps

Deliberately unbuilt, roughly in the order they start to matter:

- **No clipping.** Nothing is clipped against the near plane. A vertex at or
  behind the eye gives `w ≈ 0`, and `perspectiveTransform` returns the origin as
  a placeholder rather than splitting the triangle. Unhit only because the camera
  sits outside the model.
- **No texture coordinates.** Consuming `vt`/`vn` means **unwelding**: every
  distinct `(v, vt, vn)` triple becomes one entry in a single vertex array, so
  the rasterizer fetches one complete vertex per index — which is exactly the
  layout a hardware vertex fetch needs, and a change to the vertex layout rather
  than one more branch in the parser.
- **Only `Kd` from the MTL.** `Ka`, `Ks`, `Ns` and the `map_*` lines are skipped
  because the shading model has nowhere to put them. Unlike `vt`, these are cheap
  to add later.
- **Non-uniform scale would break normals.** They are transformed by the world
  matrix, correct only for uniform scale; the general case needs the inverse
  transpose.
- **Out-parameters in the matrix builders.** They return void into a `Mat4&`, so
  a forgotten call leaves stack garbage with no warning.
- **Coordinate parsing on `v` lines is undiagnosed.** Face indices are validated;
  vertex coordinates are not.
- **Single threaded.** Every triangle, every pixel, in order.
- **Only coverage is fixed-point.** Depth interpolation, the depth test,
  perspective correction and colour blending still run in float in
  `drawTriangleFixed`. That is a deliberate scope limit rather than an
  oversight — it is what makes every measured pixel difference attributable to
  a coverage decision — but it does mean the fixed path is not yet a complete
  model of the hardware.
- **`s` is not validated against the snapping.** `drawTriangleFixed` takes `s`
  as a parameter and trusts that `xi`/`yi` were filled at the same `s`. Passing
  a different one is not detected and produces silently wrong coverage; today
  only `main.cpp` calls it, and it uses one global for both.
- **`STATS_PREFIX` is a manual constant.** It has to be edited to match the
  model and resolution whenever those change, or the histograms are filed under
  the wrong configuration with nothing to flag it.

## How it got here

| Commit | Milestone |
|---|---|
| `5f00633`–`282c3e3` | From a flat `vector<RGB>` to a shaded animation: PPM output, the edge function and inside test, barycentric colour, the z-buffer, the perspective divide, rotation matrices, the OBJ loader with fan triangulation, and Gouraud-shaded Lambertian lighting. |
| `95ac44b`–`ef5f2ef` | Split from one 488-line `main.cpp` into six documented modules. |
| `b01b116`–`8800747` | Makefile with incremental build and video targets; `normalizationPass` returns a named `boundingBox` rather than six floats by index. |
| `0c78fcb` | Negative and out-of-range OBJ indices resolved and validated at the loader. |
| `4ad373c` | Backface culling — and the discovery that `torus.obj` was wound inward on all 800 faces. |
| `577eb64` | Perspective-correct attribute interpolation, with `rec_w` carried per vertex. |
| `1c83808` | MTL materials, one index per triangle; torus knot and solids scene added. |
| `3d6e6aa` | First downloaded models — Iron Man and a human figure — rather than generated ones. |
| `7f84287` | `ppmdiff`: frame-sequence comparison with an isolated-pixel count, written *before* the fixed-point path so the metric could not be tuned to flatter it. |
| `437ecbe` | `stats.h`/`stats.cpp`: exponent histograms behind a compile-time switch. |
| `519fac5` | The float rasterizer instrumented — nine signals, no change to its arithmetic. |
| `03df162` | `screenVertex` gains `xi`/`yi`: coordinates snapped once per vertex, so two triangles sharing an edge compute a bit-identical edge function and no crack opens. |
| `7a8c851` | `drawTriangleFixed` — fixed-point coverage beside the float path, not replacing it. |
| `c0922f5` | Rasterizer selected from `argv`, output routed per configuration. |
| `b2101e3` | Sources into `src/`, `ppmdiff` into `tools/`. |
| `3353d5c`–`d370105` | The measurements: 45 histogram CSVs, the sub-pixel sweep across five configurations, and the bit-width report. |
| `dcd1c97` | The ten root sources that move left behind, deleted — the build had been silently preferring the stale copies. |

The stills, from the earliest days:

| | | | |
|---|---|---|---|
| <img src="Media/png_files/output.png" alt="Colour gradient written straight to a PPM file" width="180"> | <img src="Media/png_files/triangle.png" alt="Single triangle filled by the edge function" width="180"> | <img src="Media/png_files/Overlappingtriangle.png" alt="Two overlapping triangles resolved by the z-buffer" width="180"> | <img src="Media/png_files/Cube.png" alt="Cube under perspective projection" width="180"> |
| A gradient written straight to a PPM file. | One triangle, filled by the edge function. | Two triangles at different depths — the z-buffer decides. | A cube under perspective projection. |

## Where this is going

The rasterizer is the software half of a hardware question. Having written the
pipeline by hand, the interesting part is what a GPU does differently: why
fragment work is embarrassingly parallel, how SIMD lanes map onto pixel quads,
why a fixed-point depth buffer has to budget its bits around the non-uniform
precision of `1/z`, and where the bandwidth actually goes.

The intended destination is an RTL implementation with this program as its
golden reference — same geometry in, framebuffer diffed pixel for pixel. That
target is why several decisions here look over-careful: the byte-layout assert,
the POD structs with predictable flat layout, the dense material table, and the
module split that keeps each stage instantiable on its own.

**The bit-width question is answered.** The study above gives 27 bits for the
edge accumulator at `s = 4`, and zero integer plus 16 fractional for depth. What
remains is Verilog, in its own top-level directory, diffed against these same
frames.

Before that comparison can be written, one thing has to be settled: it has to be
a **tolerance, not an equality**. Building this renderer twice from the same
source with the same compiler, changing nothing but `-O0` to `-O2` and disabling
multiply-add contraction on both, does not produce the same image. Instruction
selection differs, the last bit of a float differs, and the truncating `uint8_t`
cast turns that into a whole integer step.

Measured on the solids scene at 1080p, ten frames, `-ffp-contract=off` on both
builds and nothing else changed: **119,409 pixels per frame differ**, and all
but *five* of the 1,194,094 differing pixels across those ten frames differ by
exactly 1. That is the same population the sweep sees as its `diff = 1` floor.
The five outliers reach 89 levels — depth tie-breaks on near-coplanar geometry
flipping which triangle wins. Two populations, two causes, from nothing more
than an optimisation flag.

On a mesh with near-coplanar surfaces it is worse, and in a more interesting
way. A one-LSB difference in interpolated depth can flip *which* triangle wins
the depth test, and if the two carry different materials the pixel changes by a
hundred levels rather than one. Measured on Iron Man: ~12,800 pixels per frame,
max channel difference 131 — but 30% isolated single pixels and only 9% in
contiguous blocks, which is the z-fighting signature rather than a systematic
error. ASan and UBSan are clean on the same model.

So the RTL comparison cannot ask "identical?". It has to ask "how far apart, and
where?", and it has to distinguish a scatter of tie-breaks on coincident
geometry from a real disagreement — which is what `ppmdiff`'s isolated-pixel
count exists to do, and why it was written before the fixed-point path rather
than after.

### Still open

- **Temporal error is unmeasured.** The sweep compares each run against the
  reference frame by frame. It cannot see edge crawl — an edge snapping between
  quantised positions as geometry rotates — which is why Direct3D mandates
  `s = 8` where this study's static metric is content with 4. Measuring it means
  diffing consecutive frames of the same run.
- **Depth and colour are still float in the fixed path.** Only coverage was
  converted, deliberately, so that every measured difference is attributable.
  Quantising depth is the next conversion, and the `zdiff` histogram was
  recorded to size it.
- **The cull rate is asserted, not measured.** "Roughly half" is the standard
  figure for a closed mesh, but `ilogb` discards sign, so the committed
  histograms cannot confirm it. It would take one counter.
