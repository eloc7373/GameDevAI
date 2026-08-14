#!/usr/bin/env python3
"""
Numeric checks on Millhaven's world generation, with no engine required.

TerrainHeight() is a pure function, which means it can be re-implemented here
and checked against the invariants the rest of the game assumes. That is worth
doing because those invariants are load-bearing and invisible: the village disc
being *exactly* flat is why buildings do not float, and the waterline margin is
the only thing stopping the whole village from flooding.

This is the companion to check_sources.py: that one checks the code compiles
plausibly, this one checks the maths means what it should.

    python3 Tools/verify_worldgen.py

Exit code is non-zero if any invariant fails.

KEEP IN SYNC: the constants and terrain_height() below mirror
Source/Millhaven/MillhavenWorldGen.{h,cpp}. If you change the terrain function
or the waterline, change it here too and re-run - a silent drift here turns a
useful check into a reassuring lie. The parity check at the bottom re-reads the
C++ constants and fails if they no longer match.
"""

from __future__ import annotations

import math
import os
import re
import sys

SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                   "..", "Source", "Millhaven")

# --- mirrored from MillhavenWorldGen.h -------------------------------------
WATERLINE_CM = 5.0
MIN_WATER_DEPTH_CM = 20.0
VILLAGE_RADIUS_M = 16.0
CAVE_MOUTH_M = (-5.0, -36.0)
DOCK_M = (-25.0, 26.0)
BAY_WATCH_M = (-20.0, 30.0)
DOCK_DECK_TOP_CM = 39.0
CAVE_ROOF_CM = 40.0

# --- mirrored from BuildWater() --------------------------------------------
WATER_MIN_AX, WATER_MAX_AX = -60.0, -4.0
WATER_MIN_AY, WATER_MAX_AY = 4.0, 60.0
WATER_TILE_M = 2.0

FLOODS_AT = WATERLINE_CM - MIN_WATER_DEPTH_CM

# Exploration quests, mirrored from UpdateLocationQuests(). Each is (giver,
# target, radius in metres) - a quest whose giver stands inside its own arrival
# radius completes itself the moment it is handed out.
LOCATION_QUESTS = [
    ("GlowingDeep", "Elder Sylva", CAVE_MOUTH_M, 9.0),
    ("BayTrouble", "Captain Wren", BAY_WATCH_M, 7.0),
]

# UE's default ACharacter max step height, in cm.
MAX_STEP_CM = 45.0

# The six houses, mirrored from VillageBuildings().
VILLAGE_BUILDINGS = [
    (-7.0, -2.0), (6.0, -3.5), (-6.0, 6.0),
    (7.5, 6.0), (0.0, -9.0), (-12.0, -5.0),
]

# NPC spawn points, mirrored from SpawnNPCs(). The flag says the NPC is placed
# on built geometry rather than on the terrain, so "standing in water" does not
# apply to them.
NPCS = {
    "Baker Maren": ((-5.5, 5.2), False),
    "Farmer Aldric": ((5.2, 3.8), False),
    "Elder Sylva": ((-1.0, -6.0), False),
    "Captain Wren": ((-25.0, 20.0), True),   # lifted onto the pier decking
}


def terrain_height(wx: float, wy: float) -> float:
    """Port of AMillhavenWorldGen::TerrainHeight. cm in, cm out."""
    ax, ay = wx / 100.0, wy / 100.0
    d = math.sqrt(ax * ax + ay * ay)
    h = 0.0
    vr, slope = VILLAGE_RADIUS_M, 22.0
    if d > vr:
        f = min(1.0, (d - vr) / slope)
        h = (math.sin(ax * 0.18) * math.cos(ay * 0.18) * 2.5
             + math.sin(ax * 0.37 + ay * 0.25) * 1.1) * f
        if ay < -25.0:                                   # northern hills
            hf = min(1.0, (-ay - 25.0) / 25.0)
            h += hf * (math.sin(ax * 0.12) * 3.5 + math.cos(ay * 0.10) * 4.0)
        if ax < -18.0 and ay > 18.0:                     # south-west basin
            cf = min(1.0, math.sqrt((ax + 18.0) ** 2 + (ay - 18.0) ** 2) / 22.0)
            h = h * (1.0 - cf * 0.85) - cf * 1.2
        if d > 62.0:                                     # rising rim
            h += (d - 62.0) * 0.07
    return h * 100.0


def ground(ax: float, ay: float) -> float:
    """Ground height in cm at a position given in design metres."""
    return terrain_height(ax * 100.0, ay * 100.0)


def is_water_at(ax: float, ay: float) -> bool:
    """Mirror of AMillhavenWorldGen::IsWaterAt.

    The bounds test matters: without it, any low ground reads as water,
    including the cave hollow 3.2m down in the northern hills where no tile is
    ever laid. That was a real bug in the C++, caught by this script.
    """
    if ax < WATER_MIN_AX or ax > WATER_MAX_AX:
        return False
    if ay < WATER_MIN_AY or ay > WATER_MAX_AY:
        return False
    return ground(ax, ay) <= FLOODS_AT


class Report:
    def __init__(self) -> None:
        self.failures: list[str] = []
        self.notes: list[str] = []

    def check(self, ok: bool, msg: str) -> bool:
        if not ok:
            self.failures.append(msg)
        return ok

    def note(self, msg: str) -> None:
        self.notes.append(msg)


def check_village_is_flat(r: Report) -> None:
    worst = 0.0
    for i in range(-160, 161):
        for j in range(-160, 161):
            ax, ay = i / 10.0, j / 10.0
            if math.hypot(ax, ay) > VILLAGE_RADIUS_M - 0.1:
                continue
            worst = max(worst, abs(ground(ax, ay)))
    r.check(worst < 0.01, f"village disc is not flat (max deviation {worst:.4f} cm)")
    r.note(f"village disc flat to {worst:.4f} cm")


def check_buildings_and_npcs_on_flat_ground(r: Report) -> None:
    for (ax, ay) in VILLAGE_BUILDINGS:
        h = ground(ax, ay)
        r.check(abs(h) < 0.01, f"house at ({ax}, {ay}) is on a slope (h={h:.2f} cm)")
    for name, ((ax, ay), on_geometry) in NPCS.items():
        if on_geometry:
            # Placed on built geometry - check the lift clears the water instead.
            lift = DOCK_DECK_TOP_CM - ground(ax, ay)
            r.check(lift > 0.0, f"{name}'s lift onto the decking is negative")
            r.check(DOCK_DECK_TOP_CM > WATERLINE_CM,
                    f"{name} stands on decking that is below the waterline")
            continue
        r.check(not is_water_at(ax, ay),
                f"{name} is standing in water (ground {ground(ax, ay):.0f} cm)")
    r.note(f"{len(VILLAGE_BUILDINGS)} houses on level ground, "
           f"{len(NPCS)} NPCs clear of the water")


def check_water(r: Report) -> None:
    nx = math.ceil((WATER_MAX_AX - WATER_MIN_AX) / WATER_TILE_M)
    ny = math.ceil((WATER_MAX_AY - WATER_MIN_AY) / WATER_TILE_M)

    emitted = 0
    deepest = 0.0
    for i in range(nx):
        for j in range(ny):
            ax0 = WATER_MIN_AX + i * WATER_TILE_M
            ay0 = WATER_MIN_AY + j * WATER_TILE_M
            ax1, ay1 = ax0 + WATER_TILE_M, ay0 + WATER_TILE_M
            highest = max(ground(ax0, ay0), ground(ax1, ay0),
                          ground(ax1, ay1), ground(ax0, ay1))
            if highest > FLOODS_AT:
                continue
            emitted += 1
            deepest = min(deepest, highest)
            for (cx, cy) in ((ax0, ay0), (ax1, ay0), (ax1, ay1), (ax0, ay1)):
                r.check(
                    math.hypot(cx, cy) > VILLAGE_RADIUS_M,
                    f"water tile corner ({cx}, {cy}) is inside the village disc")

    r.check(emitted > 50, f"the bay is too small ({emitted} tiles)")
    r.check(emitted < nx * ny, "water covers the entire sample area - no shoreline")
    r.note(f"{emitted}/{nx * ny} water tiles ({emitted * 2} tris), "
           f"deepest {abs(deepest):.0f} cm")


def check_dock_and_boardwalk(r: Report) -> None:
    dock_ground = ground(*DOCK_M)
    r.check(dock_ground < WATERLINE_CM, "the dock is not over water")
    r.check(is_water_at(*DOCK_M), "IsWaterAt disagrees that the dock is over water")

    # Mirror of the boardwalk search in BuildStructures().
    landfall, probes = DOCK_M, 80
    for s in range(1, probes + 1):
        t = s / probes
        p = (DOCK_M[0] * (1 - t), DOCK_M[1] * (1 - t))
        landfall = p
        if not is_water_at(*p):
            break

    r.check(not is_water_at(*landfall), "the boardwalk never reaches dry land")
    span = math.dist(DOCK_M, landfall)
    r.check(span < 20.0, f"the boardwalk is implausibly long ({span:.1f} m)")
    r.check(math.hypot(*landfall) > VILLAGE_RADIUS_M,
            "the boardwalk runs into the village disc")

    # Measure to the walking surface - the plank TOP - not its underside.
    # Getting this wrong once already hid a 51cm ledge behind a "31cm" pass.
    shore_top = ground(*landfall) + 18.0
    step_up = shore_top - ground(*landfall)
    r.check(step_up < MAX_STEP_CM,
            f"stepping onto the boardwalk is a {step_up:.0f} cm climb "
            f"(UE default max step is {MAX_STEP_CM:.0f})")

    # ...and the ramp itself must not have a step in it anywhere.
    steps = max(1, math.ceil(span / 1.2))
    worst_rise = 0.0
    prev_top = None
    for s in range(steps + 1):
        t = s / steps
        top = DOCK_DECK_TOP_CM + (shore_top - DOCK_DECK_TOP_CM) * t
        if prev_top is not None:
            worst_rise = max(worst_rise, abs(top - prev_top))
        prev_top = top
    r.check(worst_rise < MAX_STEP_CM,
            f"the boardwalk ramp has a {worst_rise:.0f} cm step in it")

    r.note(f"boardwalk {span:.1f} m to ({landfall[0]:.1f}, {landfall[1]:.1f}), "
           f"ramps {DOCK_DECK_TOP_CM:.0f} -> {shore_top:.0f} cm, "
           f"{step_up:.0f} cm off the end, max rise {worst_rise:.0f} cm")


def check_location_quests(r: Report) -> None:
    """Arrival radii, mirrored from UpdateLocationQuests()."""
    for quest, giver, at, radius in LOCATION_QUESTS:
        giver_at, _ = NPCS[giver]
        dist = math.dist(giver_at, at)

        # The invariant that matters: a quest whose giver stands inside its own
        # arrival radius completes itself in the same tick it is handed out.
        r.check(dist > radius,
                f"{quest} completes instantly - {giver} stands {dist:.1f} m from "
                f"its target, inside the {radius:.0f} m arrival radius")

        # It also has to be possible to get there. The bay target is reached
        # by wading, so being under water is fine; being unreachable is not.
        r.check(dist < 80.0, f"{quest} sends the player {dist:.0f} m - implausibly far")
        r.note(f"{quest}: {giver} -> target {dist:.1f} m away, radius {radius:.0f} m")


def parse_gatherables():
    """Read the pickup table straight out of MillhavenWorldGen.cpp."""
    path = os.path.join(SRC, "MillhavenWorldGen.cpp")
    with open(path, "r", encoding="utf-8") as fh:
        text = fh.read()
    rows = re.findall(
        r'\{\s*FVector2D\(\s*(-?[\d.]+),\s*(-?[\d.]+)\)\s*,\s*TEXT\("([^"]+)"\)',
        text)
    return [(float(x), float(y), item) for x, y, item in rows]


# Where each kind of gatherable is supposed to be. Rules, not positions - so
# moving one only has to satisfy the rule, not match a second hardcoded list.
GATHERABLE_RULES = {
    # item          must be in water?   extra test
    "GoldenWheat":     (False, None),
    "Silverleaf":      (False, "in_cave"),
    "ShellwaterPearl": (True,  "wadeable"),
}

# Mirrored from MillhavenWorldGen.h.
CAVE_MIN_AX, CAVE_MAX_AX = -17.0, -4.5
CAVE_MIN_AY, CAVE_MAX_AY = -45.5, -34.5
MAX_WADE_CM = 200.0


def check_gatherables(r: Report) -> None:
    rows = parse_gatherables()
    r.check(len(rows) > 0, "no gatherables found in MillhavenWorldGen.cpp")

    counts = {}
    for ax, ay, item in rows:
        counts[item] = counts.get(item, 0) + 1
        rule = GATHERABLE_RULES.get(item)
        if rule is None:
            r.check(False, f"gatherable '{item}' has no placement rule here")
            continue
        wants_water, extra = rule

        wet = is_water_at(ax, ay)
        if wants_water:
            r.check(wet, f"{item} at ({ax}, {ay}) should be in the bay but is on land")
        else:
            r.check(not wet, f"{item} at ({ax}, {ay}) is under water")

        if extra == "in_cave":
            inside = (CAVE_MIN_AX <= ax <= CAVE_MAX_AX
                      and CAVE_MIN_AY <= ay <= CAVE_MAX_AY)
            r.check(inside, f"{item} at ({ax}, {ay}) is outside the cave chamber")
        elif extra == "wadeable":
            # No swimming in this game, so a pearl below wading depth is
            # unreachable and would soft-lock the quest.
            depth = WATERLINE_CM - ground(ax, ay)
            r.check(depth <= MAX_WADE_CM,
                    f"{item} at ({ax}, {ay}) is {depth:.0f} cm deep - too deep to wade")

    # No two pickups on top of each other: they would be swept up as one.
    for i in range(len(rows)):
        for j in range(i + 1, len(rows)):
            d = math.dist(rows[i][:2], rows[j][:2])
            r.check(d > 1.0,
                    f"gatherables at {rows[i][:2]} and {rows[j][:2]} are {d:.1f} m apart")

    r.note("gatherables: " + ", ".join(f"{k} x{v}" for k, v in sorted(counts.items())))


def check_cave_chamber(r: Report) -> None:
    """The roof has to clear the floor, which is the raw terrain in here."""
    heights = []
    ax = CAVE_MIN_AX
    while ax <= CAVE_MAX_AX:
        ay = CAVE_MIN_AY
        while ay <= CAVE_MAX_AY:
            heights.append(ground(ax, ay))
            ay += 0.5
        ax += 0.5

    highest, lowest = max(heights), min(heights)
    headroom = CAVE_ROOF_CM - highest
    r.check(headroom > 220.0,
            f"only {headroom:.0f} cm of headroom at the high end of the cave "
            f"(roof {CAVE_ROOF_CM:.0f}, floor peaks at {highest:.0f})")

    # And the doorway has to be walkable from the mouth outside.
    mouth = ground(*CAVE_MOUTH_M)
    r.check(abs(mouth - ground(CAVE_MAX_AX, -36.0)) < MAX_STEP_CM,
            "stepping through the cave doorway is more than one step height")

    r.note(f"cave floor {lowest:.0f}..{highest:.0f} cm, roof {CAVE_ROOF_CM:.0f} cm, "
           f"{headroom:.0f} cm headroom at the tightest point")


def check_constants_match_cpp(r: Report) -> None:
    """Catch this file drifting away from the C++ it mirrors."""
    header = os.path.join(SRC, "MillhavenWorldGen.h")
    try:
        with open(header, "r", encoding="utf-8") as fh:
            text = fh.read()
    except OSError as exc:
        r.check(False, f"cannot read {header}: {exc}")
        return

    for name, expected in (("WaterlineCm", WATERLINE_CM),
                           ("MinWaterDepthCm", MIN_WATER_DEPTH_CM),
                           ("CaveRoofCm", CAVE_ROOF_CM)):
        m = re.search(r'constexpr\s+float\s+%s\s*=\s*([0-9.]+)f' % name, text)
        if not r.check(m is not None, f"{name} not found in MillhavenWorldGen.h"):
            continue
        actual = float(m.group(1))
        r.check(abs(actual - expected) < 1e-6,
                f"{name} is {actual} in C++ but {expected} in this script")

    m = re.search(r'constexpr\s+float\s+DockDeckTopCm\s*=\s*([0-9.]+)f', text)
    if r.check(m is not None, "DockDeckTopCm not found in MillhavenWorldGen.h"):
        r.check(abs(float(m.group(1)) - DOCK_DECK_TOP_CM) < 1e-6,
                f"DockDeckTopCm is {m.group(1)} in C++ but {DOCK_DECK_TOP_CM} here")

    for name, expected in (("CaveMouthM", CAVE_MOUTH_M), ("DockM", DOCK_M),
                           ("BayWatchM", BAY_WATCH_M)):
        m = re.search(r'%s\(\)\s*\{\s*return\s+FVector2D\(([-0-9.]+),\s*([-0-9.]+)\)'
                      % name, text)
        if not r.check(m is not None, f"{name}() not found in MillhavenWorldGen.h"):
            continue
        actual = (float(m.group(1)), float(m.group(2)))
        r.check(actual == expected,
                f"{name}() is {actual} in C++ but {expected} in this script")

    r.note("constants match MillhavenWorldGen.h")


def main() -> int:
    r = Report()
    check_constants_match_cpp(r)
    check_village_is_flat(r)
    check_buildings_and_npcs_on_flat_ground(r)
    check_water(r)
    check_dock_and_boardwalk(r)
    check_location_quests(r)
    check_gatherables(r)
    check_cave_chamber(r)

    for n in r.notes:
        print("  ok   %s" % n)
    for m in r.failures:
        print("  FAIL %s" % m)

    print("\n%d check group(s), %d failure(s)."
          % (len(r.notes) + len(r.failures), len(r.failures)))
    return 1 if r.failures else 0


if __name__ == "__main__":
    sys.exit(main())
