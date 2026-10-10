#!/usr/bin/env python3
"""Screenshot regression check: run every scenario script and save its shots, optionally comparing
against an earlier run. Use it around refactors that should not change what the game draws.

usage: snap.py OUT_DIR [--compare BASE_DIR]
  1. before the change:  ./build.sh && python3 tools/snap.py build/snap_before
  2. after the change:   ./build.sh && python3 tools/snap.py build/snap_after --compare build/snap_before
Runs the built ROM (rs07demake.gba) with symbols from build/rs07demake.map. Shots are numbered per
script, so a compare needs the same scripts on both sides. Scripts that reference symbols the build
no longer has are skipped. Values from 'dump' lines are compared too (perf counters may wobble by
a scanline or two when code moves; that is reported but is not a failure)."""
import os
import re
import subprocess
import sys

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(root)
args = sys.argv[1:]
if not args or args[0].startswith('-'):
    sys.exit(__doc__)
out = os.path.abspath(args[0])
base = os.path.abspath(args[args.index('--compare') + 1]) if '--compare' in args else None

syms = {}
for line in open('build/rs07demake.map'):
    m = re.match(r'\s+0x([0-9a-f]{8})\s+(\w+)\s*$', line)
    if m:
        syms[m.group(2)] = int(m.group(1), 16)
harness = 'build/harness' if os.path.exists('build/harness') else '.toolchain/harness'
scripts = sorted(os.path.join(d, f) for d in ['shots', 'shots/map', 'shots/q', 'shots/t']
                 for f in os.listdir(d) if f.endswith('.src') and not f.startswith('save'))

for src in scripts:
    name = src[len('shots/'):-4].replace('/', '_')
    d = os.path.join(out, name)
    os.makedirs(d, exist_ok=True)
    txt = os.path.join(d, 'script.txt')
    subprocess.check_call([sys.executable, 'tools/qtest.py', src, txt])
    s = open(txt).read()
    try:
        s = re.sub(r'@(\w+)(?:\+(\w+))?', lambda m: '%x' % (syms[m.group(1)] + int(m.group(2) or '0', 0)), s)
    except KeyError as e:
        print('skip %s (no symbol %s)' % (src, e))
        continue
    n = [0]

    def shot(m):
        n[0] += 1
        return 'shot %s/%03d.png' % (d, n[0])
    s = re.sub(r'^shot \S+', shot, s, flags=re.M)
    s = re.sub(r'^wav \S+', 'wav %s/audio.wav' % d, s, flags=re.M)
    open(txt, 'w').write(s)
    r = subprocess.run([harness, 'rs07demake.gba', txt], capture_output=True, text=True)
    # keep only values (addresses and shot paths differ between builds)
    log = []
    for ln in (r.stdout + r.stderr).splitlines():
        if ln.startswith('shot '):
            continue
        log.append(re.sub(r'^[0-9a-f]{8}:|until8 [0-9a-f]{8}', lambda m: 'until8' if m.group(0).startswith('until8') else '', ln))
    open(os.path.join(d, 'log.txt'), 'w').write('\n'.join(log) + '\n')

if base:
    shots = diffs = 0
    for name in sorted(os.listdir(out)):
        a, b = os.path.join(base, name), os.path.join(out, name)
        if not os.path.isdir(a):
            continue
        for f in sorted(os.listdir(b)):
            if f.endswith('.png'):
                shots += 1
                if not os.path.exists(os.path.join(a, f)) or open(os.path.join(a, f), 'rb').read() != open(os.path.join(b, f), 'rb').read():
                    diffs += 1
                    print('DIFF %s/%s' % (name, f))
        la, lb = os.path.join(a, 'log.txt'), os.path.join(b, 'log.txt')
        if os.path.exists(la) and open(la).read() != open(lb).read():
            print('LOG  %s (dump/expect output changed)' % name)
    print('%d shots compared, %d differ' % (shots, diffs))
    sys.exit(1 if diffs else 0)
