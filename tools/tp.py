#!/usr/bin/env python3
"""Expand 'tp X Y DIR' (cell coords) lines into harness pokes. usage: tp.py in.txt out.txt"""
import sys
out = []
for line in open(sys.argv[1]):
    p = line.split()
    if p and p[0] == 'tp':
        x, y, d = int(p[1]), int(p[2]), int(p[3])
        out += ['press B', 'wait 2', 'poke16 @pl %x' % x, 'poke16 @pl+2 %x' % y, 'poke16 @pl+4 %x' % (x * 16), 'poke16 @pl+6 %x' % (y * 16),
                'poke8 @pl+8 %x' % d, 'wait 4']
    else:
        out.append(line.rstrip('\n'))
open(sys.argv[2], 'w').write('\n'.join(out) + '\n')
