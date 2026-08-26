# Software Rasterizer

[![CI](https://github.com/Hrishikesh-Acharyya/Rasterizer_Simulator/actions/workflows/ci.yml/badge.svg)](https://github.com/Hrishikesh-Acharyya/Rasterizer_Simulator/actions/workflows/ci.yml)
[![Docs](https://github.com/Hrishikesh-Acharyya/Rasterizer_Simulator/actions/workflows/pages.yml/badge.svg)](https://hrishikesh-acharyya.github.io/Rasterizer_Simulator/)

A 3D triangle rasterizer written from scratch in C++ — no OpenGL, no Vulkan, no
graphics library of any kind. The only drawing primitive in the whole program is
"write three bytes into an array". Everything above that — perspective
projection, the depth test, barycentric coverage, Gouraud-shaded lighting — is
built by hand.

It is not trying to be fast. It is a **reference model**: an executable,
readable definition of what a rasterizer does, stage by stage, so that a
hardware implementation has something to be diffed against. That intent shows up
throughout — in a byte-layout assertion on the pixel struct, in POD types with
predictable flat layout, and in the places where a float will eventually have to
become a fixed-point number.

## Gallery

| | |
|---|---|
| <img src="Media/Gifs/spinning_cube.gif" alt="Spinning cube, vertex colours interpolated across each face" width="380"><br>**Spinning cube** — 8 vertices, 12 triangles, vertex colours interpolated across each face. | <img src="Media/Gifs/torus.gif" alt="Unlit torus coloured by position within its bounding box" width="380"><br>**Torus, unlit** — loaded from OBJ, coloured by position within its bounding box. |
| <img src="Media/Gifs/icosphere_lambertian_lighting.gif" alt="Flat-shaded icosphere showing triangle facets" width="380"><br>**Icosphere, flat shaded** — one normal per triangle. The facets are the geometry being honest. | <img src="Media/Gifs/icosphere_gouraud_shading.gif" alt="Gouraud-shaded icosphere, facets smoothed away" width="380"><br>**Icosphere, Gouraud shaded** — same 320 triangles, normals averaged per vertex. |
| <img src="Media/Gifs/torus_lambertian_lighting.gif" alt="Flat-shaded torus with banding along triangle edges" width="380"><br>**Torus, flat shaded** — visible banding along every triangle edge. | <img src="Media/Gifs/torus_gouraud_shading.gif" alt="Gouraud-shaded torus with the banding gone" width="380"><br>**Torus, Gouraud shaded** — the banding is gone without adding a single triangle. |

The right-hand column is the whole argument for per-vertex normals. The mesh is
identical and the triangle count is identical; what changed is the normals —
one flat normal per face became an area-weighted average of the faces meeting at
each vertex — and, following from that, the lighting moved from per-face to
per-vertex with interpolation across the span. The smoothing comes from the
normals, not from geometry. Full-resolution versions are in
[`Media/Videos/`](Media/Videos).

## Repository layout

```
main.cpp          scene setup and the per-frame render loop
vectors.h         Vec3/Vec4 and pure vector arithmetic          (no dependencies)
matrices.h        Mat4, transform builders, perspective divide
types.h           RGB and screenVertex                          (no dependencies)
framebuffer.h/cpp colour + depth buffers, clears, PPM output
raster.h/cpp      edge function, bounding box, drawTriangle
model.h/cpp       OBJ loader and bounding-box measurement

Makefile          build, render, encode video/GIF, docs, graphs
Doxyfile          Doxygen configuration
.github/          CI and Pages workflows, PR template, dependabot
.gitattributes    binary file markings and language statistics
Media/            Obj_files, Videos, Gifs, png_files, Graphs
```

The include graph is kept a DAG on purpose — each `.cpp` can be compiled and
tested in isolation, which is what a testbench needs. An arrow reads "includes":

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="Media/Graphs/include_graph_dark.png">
  <img src="Media/Graphs/include_graph_light.png" alt="Module include graph" width="820">
</picture>

`vectors.h` and `types.h` sit at the bottom and include nothing from the
project. `raster.h` deliberately does not include `framebuffer.h` — a caller
needs to know what a `screenVertex` is, not that a global framebuffer exists.
`raster.cpp` does include it, because the implementation writes to those globals
(the dashed edge). Separating interface from binding is what would later let the
rasterizer be pointed at a caller-supplied buffer instead of a global.

---

## How it works

Everything runs on the CPU, single threaded, one triangle at a time.

```
OBJ file
   |  loadOBJ()               parse v/f lines, fan-triangulate polygons
   v
model space
   |  vertex normal pass      accumulate unnormalised face normals per vertex
   |  normalise matrix        centre on origin, scale largest axis to 2 units
   |  rotation X * Y * Z      3 degrees per frame, 120 frames = one full turn
   v
world space                   <- N.L evaluated here, per vertex
   |  view matrix             translate -4 on Z; camera at +4 looking down -Z
   |  perspective matrix      60 deg vertical FOV, near 0.1, far 100
   v
clip space
   |  perspectiveTransform()  one reciprocal, three multiplies: x,y,z / w
   v
NDC  [-1, 1]
   |  viewport map            -> 1920 x 1080 pixels, Y flipped
   v
screen space
   |  drawTriangle()          bounding box -> edge functions -> depth test
   v
framebuffer -> writeFramebufferToPPM()
```

### 1. Getting geometry in

`loadOBJ` reads only `v` and `f` lines. Everything else — `vn`, `vt`, `usemtl`,
groups, smoothing — is skipped, which is why the renderer has to synthesise its
own normals later.

A face token can be `5`, `5/2`, `5//3` or `5/2/3`, and only the part before the
first slash is the position index:

```cpp
std::size_t slash = token.find('/');
if (slash != std::string::npos)
    token = token.substr(0, slash);

face.push_back(std::stoi(token) - 1);   // OBJ indices are 1-based
```

Discarding `vt`/`vn` keeps the loader simple but is a real simplification.
Consuming them means **unwelding**: every distinct `(v, vt, vn)` triple has to
become one entry in a single vertex array, so the rasterizer can fetch one
complete vertex per index — which is exactly the layout a hardware vertex fetch
needs.

Polygons of any size are fan-triangulated:

```cpp
// (0,1,2), (0,2,3), (0,3,4)... an n-gon becomes n-2 triangles
for (std::size_t i = 1; i + 1 < face.size(); ++i) {
    out_indices.push_back(face[0]);
    out_indices.push_back(face[i]);
    out_indices.push_back(face[i + 1]);
}
```

This is correct only for **convex** faces; on a concave polygon some fan
triangles fall outside the outline. The quads in the test models are convex.
`torus.obj` is 800 vertices and 800 quads, so 1,600 triangles after this loop.

### 2. Placing the model

Models arrive at arbitrary scales and offsets, so `normalizationPass` measures
the axis-aligned bounding box and the render loop builds a matrix that centres
the model on the origin and scales its largest axis to span 2 units:

```cpp
Mat4 normalise = multiply_matrices(scaleM, centreM);
```

Note the order. With column vectors, `multiply_matrices(a, b)` applied to `v`
gives `a*(b*v)` — **`b` acts first**. So "centre, then scale" is written
`multiply(scale, centre)`, which reads backwards from how you say it. Scaling
before centring would scale the offset too and throw the model off screen. This
order-reversal is the single most common source of transform bugs.

The normalisation is applied through the model matrix rather than baked into the
vertex data, so the loaded geometry stays exactly as authored.

### 3. Vertex normals

The OBJ loader threw away any normals in the file, so they are computed from the
geometry. For each triangle, the face normal is added to all three of its
vertices; a vertex shared by N faces accumulates N contributions:

```cpp
Vec3 edge1 = subtract_Vec3(B, A);
Vec3 edge2 = subtract_Vec3(C, A);
Vec3 faceNormal = cross_Vec3(edge1, edge2);

vertex_normals[obj_indices[i]].x += faceNormal.x;   // ... and y, z, for all three
```

The subtle part: the face normals are accumulated **unnormalised**. A cross
product's magnitude is twice the triangle's area, so each face automatically
contributes in proportion to its size — the correct area-weighted average.
Normalising inside this loop would weight a sliver the same as a large quad.
Normalisation happens once, after every contribution is summed.

### 4. The transform chain

Transforms are 4×4 rather than 3×3 because translation is not a linear operation
on 3D points and cannot be written as a 3×3 matrix product. Lifting to
homogeneous coordinates makes it linear in 4D, and that is what lets the whole
model → world → view → clip chain collapse into a single matrix:

```cpp
Mat4 mvp = multiply_matrices(perspectiveMatrix,
               multiply_matrices(view, world_space));
```

Composed once per frame rather than once per vertex, three transforms per vertex
become one. That collapse is the entire reason a GPU has a vertex stage: one
4×4 multiply per vertex, uniform cost, no branches.

The `w` component distinguishes positions (`w=1`, affected by translation) from
directions (`w=0`, immune to it). That is why normals are transformed with
`w = 0` — translating a direction is meaningless.

### The perspective matrix

A point at eye-space depth `-z` projects onto a screen at distance `d` by
similar triangles: `x_screen = d*x / -z`. The division by `z` is the entire
content of perspective, and it is **not linear**, so no 4×4 matrix can perform
it. The trick is to have the matrix copy `-z` into the output `w` and let the
division happen afterwards:

```cpp
float t = 1.0f / std::tan(fov / 2.0f);

perspectiveMatrix.m[0][0] = t / aspectRatio;
perspectiveMatrix.m[1][1] = t;
perspectiveMatrix.m[2][2] = -(farPlane + nearPlane) / (farPlane - nearPlane);
perspectiveMatrix.m[2][3] = -(2.0f * farPlane * nearPlane) / (farPlane - nearPlane);
perspectiveMatrix.m[3][2] = -1.0f;      // <- copies -z into w
```

`t = 1/tan(fov/2)` is the distance to a screen of half-height 1; a wider FOV
gives a smaller `t` and squeezes more of the world in. Dividing by the aspect
ratio in `m[0][0]` keeps pixels square on a non-square viewport.

Only 6 of the 16 elements are meaningful, which is why the builder zeroes the
matrix first — `Mat4 m;` is 16 floats of stack garbage.

### The divide

```cpp
Vec4 clip = transform_Vec4(m, vertex);
float inv_w = (std::fabs(clip.w) > 1e-8f) ? 1.0f / clip.w : 0.0f;
return { clip.x * inv_w, clip.y * inv_w, clip.z * inv_w };
```

One reciprocal and three multiplies, not three divides. This is the only
non-uniform-cost operation in an otherwise pure multiply-add pipeline, and
correspondingly expensive in hardware — a reciprocal unit, not a DSP slice.

The `m[2][2]`/`m[2][3]` pair maps `[near, far]` onto NDC `[-1, 1]` *after* the
divide. Because that mapping is in `1/z` rather than `z`, depth precision is
heavily front-loaded: most of the available bits go to geometry near the camera.
That is z-fighting's root cause, and it is directly relevant to the hardware
version — **a fixed-point depth buffer has to budget its bits around this
non-uniformity.**

### Viewport transform

```cpp
screen_verts[i] = { (transformed.x + 1.0f) * 0.5f * VIEWPORT_WIDTH,
                    (1.0f - (transformed.y + 1.0f) * 0.5f) * VIEWPORT_HEIGHT,
                    transformed.z,
                    obj_colors[i] };
```

Y is flipped because NDC has `+Y` up while framebuffer row 0 is the top of the
screen. The coordinates stay **floats** — rounding vertices to the integer grid
here would quantise geometry and throw away sub-pixel accuracy. Only sample
points sit on the integer grid, which is why sampling happens at pixel centres.

### 5. Rasterizing a triangle

The heart of the whole program is one function evaluated three times per pixel:

```cpp
float edge_function(float ax, float ay, float bx, float by, float cx, float cy) {
    return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
}
```

Two multiplies, five adds. No division, no branch, no memory access. It returns
twice the signed area of the triangle `ABC`, and its sign says which side of the
directed edge `AB` the point `C` lies on.

`drawTriangle` walks the triangle's screen-space bounding box and evaluates it
three times at each pixel centre:

```cpp
float w0 = edge_function(B.x, B.y, C.x, C.y, x + 0.5f, y + 0.5f);
float w1 = edge_function(C.x, C.y, A.x, A.y, x + 0.5f, y + 0.5f);
float w2 = edge_function(A.x, A.y, B.x, B.y, x + 0.5f, y + 0.5f);

bool inside = (w0 >= 0 && w1 >= 0 && w2 >= 0) ||
              (w0 <= 0 && w1 <= 0 && w2 <= 0);
```

The cost is identical for every pixel regardless of geometry, and no pixel's
result depends on any other's. That uniformity is precisely what makes
rasterization parallelisable — an arbitrary number of pixels can be evaluated at
once.

Accepting all-negative as well as all-positive makes the test winding-agnostic,
at the cost of giving up the sign that would otherwise identify a back-facing
triangle for free.

Then the same three numbers are reused as barycentric weights:

```cpp
w0 = w0 / area;
w1 = w1 / area;
w2 = w2 / area;
```

Each `w` was twice the area of the sub-triangle opposite its vertex; dividing by
the total area turns them into weights summing to 1. **Normalising before
interpolating depth matters**: interpolating with the raw weights scales `z` by
the triangle's area, making the depth test area-dependent. It looks correct on
simple scenes by coincidence and fails on interpenetrating geometry.

The bounding box is clamped to the viewport, and that clamp is load-bearing —
`drawTriangle`'s buffer writes are unchecked, so an off-screen vertex without it
indexes past the end of the vector and corrupts memory rather than raising an
error.

### 6. Depth

```cpp
float z = w0 * A.z + w1 * B.z + w2 * C.z;

if (z < zbuffer[y * VIEWPORT_WIDTH + x]) {
    zbuffer[y * VIEWPORT_WIDTH + x] = z;
    // ... write colour
}
```

The buffer is cleared to `+infinity` so the first fragment at any pixel always
passes. Interpolating NDC `z` linearly in screen space is exact — because NDC
depth is already a function of `1/z`, which is the same reason a hardware depth
buffer stores this quantity rather than view-space distance.

The bandwidth shape is worth noticing: **every fragment costs a depth read, and
only survivors cost a depth write plus a colour write.** Consecutive pixels share
no data, so this is streaming traffic with no reuse. At 1080p that is millions of
accesses per frame, and it is what makes a GPU a memory-bandwidth problem rather
than an arithmetic one — the reason real hardware spends transistors on
hierarchical-Z and depth compression rather than on more ALUs.

### 7. Shading

Lighting is evaluated per **vertex** (Gouraud), and the rasterizer interpolates
the results:

```cpp
float intensityA = 0.4f + 0.6f * std::max(0.0f, dot_Vec3(world_normals[ia], lightDir));
```

For two unit vectors the dot product is exactly `cos(theta)`, which is the whole
basis of Lambertian diffuse shading: brightness falls off as the cosine of the
angle between surface normal and light, because a beam of fixed cross-section
spreads over a larger area as the surface tilts away. `max(0, N·L)` clamps
surfaces facing away rather than letting them go negative, and the `0.4` ambient
floor keeps them visible instead of black.

Phong shading would move this line into the per-pixel loop — one `N·L` per
*fragment* instead of per vertex. Far more accurate on large triangles, and
several orders of magnitude more expensive.

### 8. Getting pixels out

```cpp
static_assert(sizeof(RGB) == 3, "RGB must be tightly packed");
```

`writeFramebufferToPPM` `fwrite`s the entire pixel vector as raw bytes without
ever naming `.r`/`.g`/`.b`, so `RGB`'s byte layout is a contract with the file
format, not just a container. Three `uint8_t` cannot be padded in practice, but
nothing in the language guarantees it — and if it ever changed, every pixel after
the first would shift and produce diagonal garbage rather than an error. The
assertion turns silent corruption into a compile failure.

P6 PPM is a ~17-byte ASCII header followed by raw RGB bytes: no library needed,
and the file can be verified by hand.

## Where the work is

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="Media/Graphs/work_amplification_dark.png">
  <img src="Media/Graphs/work_amplification_light.png" alt="Work per stage, per frame" width="600">
</picture>

Node width is drawn in proportion. Work at the narrow end is worth doing
carefully once per vertex; work at the wide end has to be cheap, uniform and
parallel — which is the shape of fixed-function hardware.

## What it costs

**The pipeline runs at ~7 ms per frame** — 1,600 triangles transformed, shaded
and rasterized into 1920×1080, single threaded, `-O2`.

That is the number worth quoting. A full 120-frame run takes ~5.1 s of wall
clock, but only ~0.9 s of that is user time; the other ~4.2 s is the kernel
writing 746 MB of uncompressed PPM to disk. No renderer dumps every frame to
disk uncompressed — that is this harness, not the pipeline, and it should be
subtracted before drawing any conclusion about where the work goes.

| | |
|---|---|
| user (transform + raster) | ~0.9 s → **~7 ms/frame** |
| system (PPM writes, harness overhead) | ~4.2 s |
| wall clock | ~5.1 s |

The measurement that would say something about the rasterizer itself is a run
with the writes removed. The bandwidth argument in the depth section stands on
its own and does not depend on these numbers: it is about the depth-buffer
traffic inside the inner loop, which no amount of not-writing-files removes.

---

## Build and run

Needs a C++17 compiler and nothing else.

The Makefile targets **both** Windows and Linux/macOS from one file. Windows
sets `$(OS)` to `Windows_NT`, and the Makefile branches on it: that branch keeps
`SHELL := cmd.exe`, `.SHELLFLAGS := /C`, `del`/`if not exist`, and `%%` for
ffmpeg's frame pattern; the other branch uses the POSIX shell, `rm -f`,
`mkdir -p` and `%`. Neither branch is a fallback for the other — CI exercises
the POSIX one on every push, and the Windows one is the original and is
unchanged.

```bash
make            # build -> renderer (renderer.exe on Windows)
make run        # build, clear stale frames, render 120 frames into frames/
make video      # run, then encode an mp4        (needs ffmpeg)
make gif        # run, then encode a gif         (needs ffmpeg)
make docs       # doxygen HTML into docs/html/   (needs doxygen + graphviz)
make graphs     # redraw the README graphs       (needs graphviz)
make clean
```

Without make:

```bash
g++ -std=c++17 -Wall -Wextra -O2 main.cpp framebuffer.cpp raster.cpp model.cpp -o renderer
mkdir frames && ./renderer
```

**Paths resolve from the working directory**, so run from the repository root —
`main.cpp` loads `Media/Obj_files/torus.obj`. Change that line to swap models;
`icosphere.obj` and `test.obj` (a cube) are also there.

**`frames/` gets large.** At 1920×1080 one P6 PPM is 6,220,817 bytes and a
120-frame run writes 746,498,040 — 746 MB, or 712 MiB. It is gitignored, and `make run` clears it first — a leftover
frame the new run does not overwrite would silently appear in the video and look
like a rendering bug.

### Docs and CI

The generated Doxygen HTML is published to
**[hrishikesh-acharyya.github.io/Rasterizer_Simulator](https://hrishikesh-acharyya.github.io/Rasterizer_Simulator/)**
on every push to `main` that touches a source file. It carries the design notes
from the headers plus automatically generated include, collaboration and call
graphs.

[CI](.github/workflows/ci.yml) builds with `g++` and `clang++` under
`-Wall -Wextra -Werror`, renders all 120 frames, and validates them — file count,
exact byte size, `P6` magic, and that a frame contains more than 16 distinct
channel values, since a broken transform still writes perfectly well-formed files
full of flat grey.

## Known gaps

Deliberately unbuilt, roughly in the order they start to matter:

- **No clipping.** Nothing is clipped against the near plane. A vertex at or
  behind the eye gives `w ≈ 0`, and `perspectiveTransform` returns the origin as
  a placeholder rather than splitting the triangle. Unhit only because the
  camera sits outside the model.
- **Interpolation is affine, not perspective correct.** Barycentric weights are
  computed in screen space and applied directly to colour. Correct values need
  per-vertex division by `w` and renormalisation. Invisible on small centred
  models, obvious on a large tilted one.
- **No backface culling.** The winding-agnostic inside test throws away the sign
  that would identify back faces, so they are rasterized and then discarded by
  the depth test — roughly twice the fragment work needed.
- **Negative OBJ indices unhandled.** They are legal and count backwards from the
  most recent element; `std::stoi("-1") - 1 = -2` indexes out of bounds.
  Unreachable with the generated test models, a live hazard for a downloaded one.
- **Non-uniform scale would break normals.** They are transformed by the world
  matrix, correct only for uniform scale; the general case needs the inverse
  transpose.
- **Out-parameters and magic indices.** The matrix builders return void into a
  `Mat4&`, so a forgotten call leaves stack garbage with no warning;
  `normalizationPass` returns six floats addressed by number, where a wrong index
  yields a wrong render rather than an error.
- **Single threaded.** Every triangle, every pixel, in order.

## How it got here

| Commit | Milestone |
|---|---|
| `5f00633` | Framebuffer as a flat `vector<RGB>`, with the `static_assert` that makes the raw `fwrite` safe. |
| `621e4f8` | Colour map written to a PPM file — proof the bytes land where they should. |
| `83bbe08` | Edge function, inside test, and a viewport-clamped bounding box. |
| `ec5aad4` | Edge function values reused as barycentric weights → smooth colour. |
| `52bd7c1` | Z-buffer and depth test: overlap resolves per pixel, not by draw order. |
| `f8b16a8` | Perspective matrix and the divide by `w`. Flat images become 3D. |
| `2629503` | Rotation matrices about X, Y, Z composed into a model matrix. |
| `9c34965` | 120-frame animation loop with per-frame buffer clears. |
| `1429249` | OBJ loader, fan triangulation, and the bounding-box normalisation pass. |
| `38239af` | Icosphere and torus rendered through the loader. |
| `6c25434` | Face normals and a Lambertian diffuse term. |
| `282c3e3` | Gouraud shading: area-weighted per-vertex normals. |
| `95ac44b`–`ef5f2ef` | Split from one 488-line `main.cpp` into six documented modules. |
| `b01b116` | Makefile: incremental build, render, video and GIF targets. |

The stills, in the order they were made:

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
the POD structs with predictable flat layout, the flagged spots where a float
pipeline will have to become a fixed-point one, and the module split that keeps
each stage instantiable on its own.
