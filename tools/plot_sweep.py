#!/usr/bin/env python3
"""Draw the sub-pixel sweep curve from the committed summary CSVs.

Reads stats/*_diff/*summary*.csv and writes Media/Graphs/sweep_{light,dark}.svg.
The point of generating rather than hand-drawing: the chart cannot drift from the
data, because `make graphs` rebuilds it from the same CSVs the study reports.

Standard library only. The renderer needs a C++17 compiler and nothing else, and
regenerating a figure should not be the thing that adds a dependency stack.

Log y, because the claim being drawn is that each extra sub-pixel bit HALVES the
coverage error: on a log axis a constant ratio is a constant slope, so "all five
configurations share a slope" is something the eye checks directly rather than
something the caption asserts.

Two configurations reach exactly zero at s = 16. A log axis has no zero, so those
land on the axis floor as hollow markers -- drawn differently because they mean
something different, not clipped silently.
"""

import csv
import os

OUT = "Media/Graphs"

# (csv path, display label) in the fixed categorical order below.
RUNS = [
    ("stats/IronMan_1080p_diff/sweep_ironman_1080p_summary.csv",   "Iron Man 1080p"),
    ("stats/torus_knot_1080p_diff/sweep_torus_1080p_summary.csv",  "torus knot 1080p"),
    ("stats/solids_scene_1080p_diff/sweep_solids_1080p_summary.csv", "solids 1080p"),
    ("stats/solids_scene_720p_diff/sweep_solids_720p_summary.csv", "solids 720p"),
    ("stats/solids_scene_480p_diff/sweep_solids_480p_summary.csv", "solids 480p"),
]

# Categorical slots 1-5, assigned in fixed order and never cycled. Both modes are
# selected for their own surface rather than being an automatic flip of the other.
THEME = {
    "light": dict(surface="#fcfcfb", ink="#0b0b0b", ink2="#52514e", grid="#e3e2df",
                  axis="#b9b8b4",
                  series=["#2a78d6", "#eb6834", "#1baf7a", "#eda100", "#e87ba4"]),
    "dark":  dict(surface="#1a1a19", ink="#ffffff", ink2="#c3c2b7", grid="#33322f",
                  axis="#55534e",
                  series=["#3987e5", "#d95926", "#199e70", "#c98500", "#d55181"]),
}

W, H = 900, 500
L, R, T, B = 74, 178, 70, 74          # margins; R leaves room for direct labels
X0, X1 = 0.0, 16.0                    # sub-pixel bits
DEC_LO, DEC_HI = 0.1, 100000.0        # lowest and highest gridded decade
FLOOR_GAP = 20                        # px between the zero floor and the axis


def load():
    out = []
    for path, label in RUNS:
        pts = []
        for row in csv.DictReader(open(path)):
            pts.append((float(row["frac_bits"]), float(row["diff_ge_2_per_frame"])))
        out.append((label, sorted(pts)))
    return out


def sx(s):
    return L + (s - X0) / (X1 - X0) * (W - L - R)


def sy(v):
    """Log position, with exact zeros parked on a reserved floor below the
    lowest decade. A log axis has no zero; pretending otherwise would either
    drop those points or draw them as if they were merely small."""
    from math import log10
    span = H - T - B - FLOOR_GAP          # the log decades occupy this much
    if v <= 0:
        return T + span + FLOOR_GAP       # the reserved floor, below every decade
    lo, hi = log10(DEC_LO), log10(DEC_HI)
    t = min(max(log10(v), lo), hi)
    return T + (1 - (t - lo) / (hi - lo)) * span


def esc(t):
    return t.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def render(mode, data):
    c = THEME[mode]
    o = []
    a = o.append
    a(f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" '
      f'viewBox="0 0 {W} {H}" font-family="Helvetica,Arial,sans-serif">')
    a(f'<rect width="{W}" height="{H}" fill="{c["surface"]}"/>')

    a(f'<text x="{L}" y="26" font-size="15" font-weight="600" fill="{c["ink"]}">'
      'Coverage error halves with every sub-pixel bit</text>')
    a(f'<text x="{L}" y="43" font-size="11.5" fill="{c["ink2"]}">'
      'Pixels per frame differing from the float reference by 2 or more levels. '
      'A constant halving is a constant slope here.</text>')

    # Recessive grid: one line per decade.
    v = DEC_LO
    while v <= DEC_HI * 1.001:
        y = sy(v)
        a(f'<line x1="{L}" y1="{y:.1f}" x2="{W-R}" y2="{y:.1f}" '
          f'stroke="{c["grid"]}" stroke-width="1"/>')
        lab = f"{v:,.0f}" if v >= 1 else f"{v:g}"
        a(f'<text x="{L-10}" y="{y+4:.1f}" font-size="10.5" text-anchor="end" '
          f'fill="{c["ink2"]}">{lab}</text>')
        v *= 10

    # The floor holds a different kind of value from the decades above it, so it
    # is drawn differently: dashed, and labelled as a state rather than a number.
    fy = sy(0)
    a(f'<line x1="{L}" y1="{fy:.1f}" x2="{W-R}" y2="{fy:.1f}" '
      f'stroke="{c["axis"]}" stroke-width="1" stroke-dasharray="3 3"/>')
    a(f'<text x="{L-10}" y="{fy+4:.1f}" font-size="10.5" text-anchor="end" '
      f'fill="{c["ink2"]}">0</text>')

    for s in (0, 1, 2, 3, 4, 6, 8, 12, 16):
        a(f'<text x="{sx(s):.1f}" y="{H-B+18}" font-size="10.5" text-anchor="middle" '
          f'fill="{c["ink2"]}">{s}</text>')
    a(f'<text x="{(L+W-R)/2:.1f}" y="{H-B+38}" font-size="11.5" text-anchor="middle" '
      f'fill="{c["ink2"]}">sub-pixel fractional bits (s)</text>')
    a(f'<line x1="{L}" y1="{H-B:.1f}" x2="{W-R}" y2="{H-B:.1f}" '
      f'stroke="{c["axis"]}" stroke-width="1"/>')

    ends = []
    for i, (label, pts) in enumerate(data):
        col = c["series"][i]
        d = " ".join(("M" if j == 0 else "L") + f"{sx(s):.1f},{sy(v):.1f}"
                     for j, (s, v) in enumerate(pts))
        a(f'<path d="{d}" fill="none" stroke="{col}" stroke-width="2" '
          'stroke-linejoin="round" stroke-linecap="round"/>')
        for s, v in pts:
            x, y = sx(s), sy(v)
            if v == 0:
                # Hollow: converged exactly, not merely small. A surface-coloured
                # core keeps it legible where lines overlap.
                a(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="4.5" fill="{c["surface"]}" '
                  f'stroke="{col}" stroke-width="2"/>')
            else:
                a(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="4" fill="{col}" '
                  f'stroke="{c["surface"]}" stroke-width="1.5"/>')
        ends.append((sy(pts[-1][1]), sx(pts[-1][0]), col, label))

    # Direct labels rather than a legend box: identity never rests on colour
    # alone, and it is the relief the light palette's contrast check requires.
    # Two runs finish on the same floor, so the labels are pushed apart to a
    # minimum spacing and a leader line keeps each tied to its own end point.
    ends.sort()
    MIN, LIMIT = 15.0, H - B - 2
    ys = [e[0] for e in ends]
    for i in range(1, len(ys)):                 # spread downward
        ys[i] = max(ys[i], ys[i - 1] + MIN)
    if ys and ys[-1] > LIMIT:                   # ran past the axis: resolve upward
        ys[-1] = LIMIT
        for i in range(len(ys) - 2, -1, -1):
            ys[i] = min(ys[i], ys[i + 1] - MIN)
    placed = [(ys[i], ends[i][0], ends[i][1], ends[i][2], ends[i][3])
              for i in range(len(ends))]
    for ly, y, x, col, label in placed:
        if abs(ly - y) > 0.5:
            a(f'<path d="M{x+5:.1f},{y:.1f} L{x+11:.1f},{ly:.1f}" fill="none" '
              f'stroke="{col}" stroke-width="1" opacity="0.55"/>')
        a(f'<circle cx="{x+14:.1f}" cy="{ly:.1f}" r="3.5" fill="{col}"/>')
        a(f'<text x="{x+23:.1f}" y="{ly+4:.1f}" font-size="11" fill="{c["ink"]}">'
          f'{esc(label)}</text>')

    a(f'<text x="{L}" y="{H-10}" font-size="10" fill="{c["ink2"]}">'
      'Source: stats/*_diff/*summary*.csv · 10 frames per run · '
      'hollow marker on the dashed floor = exactly zero'
      '</text>')
    a('</svg>')
    return "\n".join(o)


def main():
    data = load()
    os.makedirs(OUT, exist_ok=True)
    for mode in ("light", "dark"):
        p = os.path.join(OUT, f"sweep_{mode}.svg")
        open(p, "w").write(render(mode, data))
        print(f"wrote {p}")


if __name__ == "__main__":
    main()
