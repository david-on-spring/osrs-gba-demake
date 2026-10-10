#!/usr/bin/env python3
"""Analyse a harness WAV: per-second RMS/peak (dBFS), clipping, silence and click detection.
usage: wavstat.py file.wav [--onsets]"""
import math, struct, sys
d = open(sys.argv[1], 'rb').read()
rate = struct.unpack('<I', d[24:28])[0]
n = (len(d) - 44) // 4
s = struct.unpack('<%dh' % (2 * n), d[44:44 + 4 * n])
mono = [(s[2 * i] + s[2 * i + 1]) / 2 for i in range(n)]
peak_all = max(abs(x) for x in mono) if mono else 0
clips = sum(1 for x in mono if abs(x) >= 32000)
print('%s: %.1fs at %d Hz, peak %.1f dBFS, %d near-clipping samples' % (
    sys.argv[1], n / rate, rate, 20 * math.log10(max(1, peak_all) / 32768), clips))
line = []
for sec in range(0, n // rate):
    seg = mono[sec * rate:(sec + 1) * rate]
    rms = math.sqrt(sum(x * x for x in seg) / len(seg))
    line.append('%5.1f' % (20 * math.log10(max(1, rms) / 32768)))
print('RMS dBFS per second:', ' '.join(line))
# clicks: large sample-to-sample jumps relative to local level
jumps = 0
for i in range(1, n):
    if abs(mono[i] - mono[i - 1]) > 12000:
        jumps += 1
print('large discontinuities (>12000):', jumps)
if '--onsets' in sys.argv:
    # energy envelope at 10 ms, report onset times (rising edges)
    hop = rate // 100
    env = [sum(abs(x) for x in mono[k:k + hop]) / hop for k in range(0, n - hop, hop)]
    on = [i / 100 for i in range(1, len(env)) if env[i] > env[i - 1] * 1.8 + 150]
    print('onsets (s):', ' '.join('%.2f' % t for t in on[:80]))
