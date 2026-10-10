#!/usr/bin/env python3
"""Compare note onsets in a harness WAV against the MIDI note starts of a track.
usage: check_timing.py file.wav music/track.mid
Fits wav_time = a*midi_time + b over matched onsets; a should be 1.000 (tempo exact)."""
import math, os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import midi2gba as M
d = open(sys.argv[1], 'rb').read()
rate = struct.unpack('<I', d[24:28])[0]
n = (len(d) - 44) // 4
s = struct.unpack('<%dh' % (2 * n), d[44:44 + 4 * n])
mono = [(s[2 * i] + s[2 * i + 1]) / 2 for i in range(n)]
# spectral-flux-like onset detection on a 5 ms energy envelope (high-passed to favour attacks)
hop = rate // 200
hp, prev, env = [], 0, []
for k in range(0, n - hop, hop):
    e = 0
    for x in mono[k:k + hop]:
        y = x - prev; prev = x; e += y * y
    env.append(math.sqrt(e / hop))
flux = [max(0, env[i] - env[i - 1]) for i in range(1, len(env))]
thr = sorted(flux)[int(len(flux) * .97)]
det = []
for i in range(1, len(flux) - 1):
    if flux[i] > thr and flux[i] >= flux[i - 1] and flux[i] >= flux[i + 1]:
        t = (i + 1) * hop / rate
        if not det or t - det[-1] > .06:
            det.append(t)
div, events = M.parse_midi(sys.argv[2])
tsec, _ = M.tick_to_sec_fn(div, events)
starts = sorted(set(round(tsec(e[0]), 3) for e in events if e[2] == 'ch' and e[3] == 0x90 and e[6] > 0))
# best offset by histogram of differences
best, bo = 0, 0
for off_ms in range(int(float(os.environ.get("OFF0", "-0.1")) * 1000), int(float(os.environ.get("OFF1", "0.6")) * 1000), 2):
    off = off_ms / 1000
    c = sum(1 for t in det if any(abs(t - (m + off)) < .02 for m in starts))
    if c > best:
        best, bo = c, off
pairs = []
for t in det:
    m = min(starts, key=lambda m: abs(t - (m + bo)))
    if abs(t - (m + bo)) < .03:
        pairs.append((m, t))
if len(pairs) < 3:
    print('too few matched onsets'); sys.exit(1)
mx = sum(p[0] for p in pairs) / len(pairs); my = sum(p[1] for p in pairs) / len(pairs)
a = sum((p[0] - mx) * (p[1] - my) for p in pairs) / sum((p[0] - mx) ** 2 for p in pairs)
b = my - a * mx
res = [p[1] - (a * p[0] + b) for p in pairs]
print('%d detected onsets, %d matched to MIDI note starts (offset %.3f s)' % (len(det), len(pairs), bo))
print('tempo ratio a = %.5f (1.0 = exact), residual rms %.1f ms, max %.1f ms' % (
    a, 1000 * math.sqrt(sum(r * r for r in res) / len(res)), 1000 * max(abs(r) for r in res)))
