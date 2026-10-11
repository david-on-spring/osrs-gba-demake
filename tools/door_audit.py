#!/usr/bin/env python3
"""Compare the doors of each traced town with the door marks of its OSRS map trace.
usage: door_audit.py [TOWN ...]   (prints doors with no map mark, and map doors left without one)"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import world_gen as wg  # noqa: E402

towns = sys.argv[1:] or ['lumbridge', 'draynor', 'varrock', 'alkharid', 'falador', 'edgeville']
for town in towns:
    ref = wg.load_ref(town)
    rx0, ry0, rx1, ry1 = ref['rect']
    z = next(c for c in wg.CITIES if c.name.lower() == town).ctx
    cell = lambda X, Y: (z.CX0 + X - z.OX, z.CY0 + z.OY - Y)
    T = lambda X, Y: wg.TY[cell(X, Y)[1]][cell(X, Y)[0]]
    marks = set()           # (X, Y) tiles on either side of a door edge
    for Y in range(ry0, ry1 + 1):
        for X in range(rx0, rx1 + 1):
            for (e, a, b) in ((ref['N'][ry1 - Y][X - rx0], (X, Y), (X, Y + 1)), (ref['W'][ry1 - Y][X - rx0], (X, Y), (X - 1, Y))):
                ia = ref['G'][ry1 - a[1]][a[0] - rx0] in 'fwk' if rx0 <= a[0] <= rx1 and ry0 <= a[1] <= ry1 else False
                ib = ref['G'][ry1 - b[1]][b[0] - rx0] in 'fwk' if rx0 <= b[0] <= rx1 and ry0 <= b[1] <= ry1 else False
                if e in 'do' and ia != ib:      # an entrance: floor on one side only
                    marks |= {a, b}
    R = [c for c in wg.CITIES if c.name.lower() == town][0].R
    roof = lambda X, Y: wg.ROOF[cell(X, Y)[1]][cell(X, Y)[0]]
    outside = lambda X, Y: T(X, Y) in wg.WALKSET and roof(X, Y) == 0
    def edge(a, b):                     # the game's edge between two neighbouring OSRS tiles
        (ax, ay), (bx, by) = cell(*a), cell(*b)
        return wg.EDGES.get(wg.ekey(ax, ay, bx - ax, by - ay))
    # exterior entrances: a door, or an open gap, between a building's floor and open ground
    doors = set()
    for Y in range(R[1], R[3] + 1):
        for X in range(R[0], R[2] + 1):
            if not roof(X, Y) or T(X, Y) not in wg.WALKSET:
                continue
            for (u, v) in wg.D4:
                n = (X + u, Y + v)
                e = edge((X, Y), n)
                if outside(*n) and (e is None or e[0] == 'door'):
                    doors |= {(X, Y), n}
    near = lambda t, S: any((t[0] + u, t[1] + v) in S for u in (-1, 0, 1) for v in (-1, 0, 1))
    extra = sorted(d for d in doors if not near(d, marks))
    missing = sorted(m for m in marks if R[0] <= m[0] <= R[2] and R[1] <= m[1] <= R[3] and not near(m, doors))
    print('%-10s entrance tiles %3d  map entrance tiles %3d  entrances not on the map %3d  map entrances missing %3d'
          % (town, len(doors), len(marks), len(extra), len(missing)))
    if '-v' in os.environ.get('AUDIT', ''):
        print('   extra', extra[:30])
        print('   missing', missing[:30])
