#!/usr/bin/env python3
"""Expand 'tp X Y DIR [PLANE]' (OSRS tile) lines into harness pokes. usage: tp.py in.txt out.txt
(tools/qtest.py does the same and more; this is the minimal version for hand-written scripts.)"""
import os
import sys
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
x0, y1 = next((int(p[1]), int(p[2])) for p in (ln.split() for ln in open(os.path.join(root, 'build/spawns.txt')))
              if p[0] == 'world')
out = []
for line in open(sys.argv[1]):
    p = line.split()
    if p and p[0] == 'tp':
        x, y, d = int(p[1]), int(p[2]), int(p[3])
        out += ['press B', 'wait 2', 'poke16 @pl %x' % x, 'poke16 @pl+2 %x' % y, 'poke16 @pl+4 %x' % ((x - x0) * 16),
                'poke16 @pl+6 %x' % ((y1 - y) * 16), 'poke8 @pl+8 %x' % d, 'poke8 @pl+0x28d %x' % (int(p[4]) if len(p) > 4 else 0),
                'wait 4']
    else:
        out.append(line.rstrip('\n'))
open(sys.argv[2], 'w').write('\n'.join(out) + '\n')
