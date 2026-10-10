#!/usr/bin/env python3
"""Run a harness script, resolving @symbol+offset tokens from the linker map.
usage: run_scenario.py script.txt [save.sav]"""
import re, subprocess, sys, os
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
syms = {}
for line in open(os.path.join(root, 'build/rs07demake.map')):
    m = re.match(r'\s+0x([0-9a-f]{8})\s+(\w+)\s*$', line)
    if m:
        syms[m.group(2)] = int(m.group(1), 16)
def sub(m):
    return '%x' % (syms[m.group(1)] + int(m.group(2) or '0', 0))
src = open(sys.argv[1]).read()
out = re.sub(r'@(\w+)(?:\+(\w+))?', sub, src)
tmp = sys.argv[1] + '.resolved'
open(tmp, 'w').write(out)
harness = os.path.join(root, 'build/harness')
if not os.path.exists(harness):
    harness = os.path.join(root, '.toolchain/harness')
args = [harness, os.path.join(root, 'rs07demake.gba'), tmp] + sys.argv[2:]
sys.exit(subprocess.call(args, cwd=root))
