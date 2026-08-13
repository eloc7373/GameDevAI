#!/usr/bin/env python3
"""
Renders Millhaven to an SVG map, straight from the generation maths.

The world is built in C++ at runtime, so short of launching the editor there is
no way to look at it. This draws the same terrain function, the same colour
rules and the same water test the game uses, which makes it useful for exactly
the things that are hard to see in code: whether the bay has a sensible shape,
whether the NPCs are where you meant them to be, and whether a quest sends the
player somewhere reachable.

It is a diagram, not a screenshot. Heights are shaded, nothing is in
perspective, and the props (trees, rocks, fences) are not drawn.

    python3 Tools/render_map.py [-o millhaven-map.svg]

Terrain colours mirror BuildTerrain(); the water test is IsWaterAt(). Both come
from verify_worldgen.py, so there is one Python definition of the terrain, not
two that can drift.
"""

from __future__ import annotations

import argparse
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from verify_worldgen import (  # noqa: E402
    BAY_WATCH_M, CAVE_MOUTH_M, DOCK_M, NPCS, VILLAGE_BUILDINGS,
    VILLAGE_RADIUS_M, WATERLINE_CM, ground, is_water_at,
)

WORLD_M = 180.0          # matches sizeM in BuildTerrain()
CELL_M = 1.5
PX_PER_M = 6.0

# Mirrored from BuildTerrain().
GRASS = (70, 150, 46)
GRASS_DARK = (56, 128, 40)
SAND = (212, 168, 88)
HIGHLAND = (72, 96, 48)
WATER = (40, 112, 184)

INK = "#1c1a17"
PAPER = "#f4efe4"


def terrain_colour(ax: float, ay: float, i: int, j: int):
    """Mirror of the per-quad colour choice in BuildTerrain().

    One deliberate difference: the game alternates Grass/GrassDark per quad for
    a low-poly texture, which at map scale is just dither. The map uses the one
    green so the landforms read.
    """
    h = ground(ax, ay)
    col = GRASS
    if h > 380.0:
        col = HIGHLAND
    coast_d = math.hypot(ax + 18.0, ay - 18.0)
    if coast_d < 38.0 and h < WATERLINE_CM + 80.0:
        col = SAND
    return col, h


SHADE_STEPS = 10


def shade(col, h: float):
    """Lighten with height so the hills and the basin read on a flat map.

    Quantised: continuous shading makes every cell a unique colour, which stops
    the run-merging in render() from doing anything.
    """
    t = max(-1.0, min(1.0, h / 500.0))
    t = round(t * SHADE_STEPS) / SHADE_STEPS
    f = 1.0 + 0.32 * t
    return tuple(max(0, min(255, int(c * f))) for c in col)


def to_screen(ax: float, ay: float, size_px: float):
    """North-up, using Millhaven's axis convention: +X east, +Y south.

    This is NOT the UE default of +X=north. The project inherits screen-space
    axes from its Three.js prototype, and all of its world content agrees:
    BiomeAt() puts "Northern Hills" at ay < -28, and the cave marked "(north)"
    is at y = -36. So east is right and south is down, directly.
    """
    half = size_px / 2.0
    return (half + ax * PX_PER_M, half + ay * PX_PER_M)


def render() -> str:
    size_px = WORLD_M * PX_PER_M
    n = int(WORLD_M / CELL_M)
    cell_px = CELL_M * PX_PER_M
    out = []

    out.append(
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{size_px:.0f}" '
        f'height="{size_px:.0f}" viewBox="0 0 {size_px:.0f} {size_px:.0f}">')
    out.append(f'<rect width="100%" height="100%" fill="{PAPER}"/>')

    # --- terrain + water ---------------------------------------------------
    # Cells are merged into horizontal runs of one colour. Emitting a rect per
    # cell is correct but produces a megabyte of SVG for a 120x120 grid.
    out.append('<g shape-rendering="crispEdges">')
    water_cells = 0
    half = size_px / 2.0
    for i in range(n):
        ay = -WORLD_M / 2 + (i + 0.5) * CELL_M
        y = half + (-WORLD_M / 2 + i * CELL_M) * PX_PER_M

        row = []
        for j in range(n):
            ax = -WORLD_M / 2 + (j + 0.5) * CELL_M
            if is_water_at(ax, ay):
                depth = (WATERLINE_CM - ground(ax, ay)) / 300.0
                depth = round(max(0.0, min(1.0, depth)) * SHADE_STEPS) / SHADE_STEPS
                f = 1.0 - 0.45 * depth
                col = tuple(int(c * f) for c in WATER)
                water_cells += 1
            else:
                base, h = terrain_colour(ax, ay, i, j)
                col = shade(base, h)
            row.append(col)

        j = 0
        while j < n:
            k = j
            while k + 1 < n and row[k + 1] == row[j]:
                k += 1
            x = half + (-WORLD_M / 2 + j * CELL_M) * PX_PER_M
            w = (k - j + 1) * cell_px
            out.append(f'<rect x="{x:.0f}" y="{y:.0f}" width="{w:.0f}" '
                       f'height="{cell_px:.0f}" fill="rgb{row[j]}"/>')
            j = k + 1
    out.append('</g>')

    def marker(ax, ay, label, colour, r=5.0, dy=-10.0, anchor="middle"):
        x, y = to_screen(ax, ay, size_px)
        out.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="{r}" fill="{colour}" '
                   f'stroke="{INK}" stroke-width="1.5"/>')
        out.append(
            f'<text x="{x:.1f}" y="{y + dy:.1f}" font-family="Georgia,serif" '
            f'font-size="13" font-weight="bold" text-anchor="{anchor}" fill="{INK}" '
            f'stroke="{PAPER}" stroke-width="3.5" paint-order="stroke">{label}</text>')

    # --- village disc ------------------------------------------------------
    cx, cy = to_screen(0.0, 0.0, size_px)
    out.append(f'<circle cx="{cx:.1f}" cy="{cy:.1f}" '
               f'r="{VILLAGE_RADIUS_M * PX_PER_M:.1f}" fill="none" stroke="{INK}" '
               f'stroke-width="1.5" stroke-dasharray="6 5" opacity="0.55"/>')

    # --- buildings ---------------------------------------------------------
    for (ax, ay) in VILLAGE_BUILDINGS:
        x, y = to_screen(ax, ay, size_px)
        s = 4.2 * PX_PER_M / 2
        out.append(f'<rect x="{x - s:.1f}" y="{y - s:.1f}" width="{s * 2:.1f}" '
                   f'height="{s * 2:.1f}" fill="#8d5a34" stroke="{INK}" '
                   f'stroke-width="1.5" rx="2"/>')

    # --- boardwalk (same search BuildStructures runs) -----------------------
    landfall = DOCK_M
    for s in range(1, 81):
        t = s / 80.0
        p = (DOCK_M[0] * (1 - t), DOCK_M[1] * (1 - t))
        landfall = p
        if not is_water_at(*p):
            break
    x0, y0 = to_screen(*DOCK_M, size_px=size_px)
    x1, y1 = to_screen(*landfall, size_px=size_px)
    out.append(f'<line x1="{x0:.1f}" y1="{y0:.1f}" x2="{x1:.1f}" y2="{y1:.1f}" '
               f'stroke="#a0784a" stroke-width="7" stroke-linecap="round"/>')
    # the pier itself, 14m along the north-south axis
    px0, py0 = to_screen(DOCK_M[0], DOCK_M[1] - 7.0, size_px)
    px1, py1 = to_screen(DOCK_M[0], DOCK_M[1] + 7.0, size_px)
    out.append(f'<line x1="{px0:.1f}" y1="{py0:.1f}" x2="{px1:.1f}" y2="{py1:.1f}" '
               f'stroke="#a0784a" stroke-width="10" stroke-linecap="round"/>')

    # --- landmarks and NPCs ------------------------------------------------
    marker(*CAVE_MOUTH_M, label="Cave Mouth", colour="#2b2438")
    marker(*BAY_WATCH_M, label="Moored Boat", colour="#c07840")
    for name, ((ax, ay), _on_geom) in NPCS.items():
        marker(ax, ay, name, "#e03434", r=4.5)

    # --- biome labels ------------------------------------------------------
    for label, (ax, ay) in (
            ("Millhaven Village", (0.0, 0.0)),
            ("Shellwater Harbour", (-36.0, 36.0)),     # -X/+Y = south-west
            ("Northern Hills", (0.0, -46.0)),          # -Y = north
            ("Pinewood Forest", (32.0, -28.0)),        # +X/-Y = north-east
            ("Outer Wilderness", (48.0, 48.0))):
        x, y = to_screen(ax, ay, size_px)
        out.append(
            f'<text x="{x:.1f}" y="{y:.1f}" font-family="Georgia,serif" '
            f'font-size="17" font-style="italic" text-anchor="middle" '
            f'fill="{INK}" opacity="0.75" stroke="{PAPER}" stroke-width="4" '
            f'paint-order="stroke">{label}</text>')

    # --- north arrow + scale ----------------------------------------------
    out.append(f'<g transform="translate(46,54)">'
               f'<line x1="0" y1="26" x2="0" y2="-18" stroke="{INK}" stroke-width="2.5"/>'
               f'<path d="M -7 -10 L 0 -24 L 7 -10 Z" fill="{INK}"/>'
               f'<text x="0" y="44" font-family="Georgia,serif" font-size="15" '
               f'font-weight="bold" text-anchor="middle" fill="{INK}">N</text></g>')

    bar = 20.0 * PX_PER_M
    by = size_px - 40
    out.append(f'<line x1="40" y1="{by}" x2="{40 + bar:.0f}" y2="{by}" '
               f'stroke="{INK}" stroke-width="3"/>')
    out.append(f'<text x="{40 + bar / 2:.0f}" y="{by - 9}" font-family="Georgia,serif" '
               f'font-size="13" text-anchor="middle" fill="{INK}">20 m</text>')
    out.append(f'<text x="40" y="{by + 22}" font-family="Georgia,serif" '
               f'font-size="12" fill="{INK}" opacity="0.7">'
               f'Generated from TerrainHeight() - '
               f'{water_cells} water cells, waterline {WATERLINE_CM:.0f} cm</text>')

    out.append('</svg>')
    return "\n".join(out)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", "--out",
                    default=os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                         "millhaven-map.svg"))
    args = ap.parse_args()

    svg = render()
    with open(args.out, "w", encoding="utf-8") as fh:
        fh.write(svg)
    print("wrote %s (%.0f KB)" % (args.out, len(svg) / 1024.0))
    return 0


if __name__ == "__main__":
    sys.exit(main())
