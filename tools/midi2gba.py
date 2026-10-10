#!/usr/bin/env python3
"""Build-time music converter: Standard MIDI files -> compact note streams + a procedural
wavetable instrument bank for the software mixer in src/sound.c.

usage: midi2gba.py music/tracks.txt outdir   -> outdir/music_data.c, outdir/music_data.h

Pure Python 3, no dependencies. What it does with a MIDI file:
  * parses format 0/1 SMF (running status, sysex, meta), merges tracks, applies the tempo map
  * converts time to sequencer sub-ticks (4 per GBA frame, ~238.9 Hz)
  * pairs note on/off, extends notes held by the sustain pedal (CC64)
  * bakes velocity x channel volume (CC7) x expression (CC11) into one amplitude (GM square law)
  * maps each channel's General MIDI program to one of the synth instruments, channel 10 to
    the drum kit
  * honours "loopStart"/"loopEnd" marker or cue meta events if present; otherwise the whole
    song loops at the end of the last note (rounded up to a whole bar)
  * normalises loudness per song so tracks play at similar levels

Note-stream format (bytes): repeated  [delta varlen][inst][note][amp][dur varlen]
  inst < 0x40: melodic instrument id; inst == 0x40: drum (note = GM drum note);
  inst == 0xFF: end of song (delta = wait before looping), followed by nothing.
  varlen = 7 bits per byte, high bit set on all but the last byte, most significant first.
"""
import math, os, random, struct, sys

RATE = 16777216 / 1254            # DirectSound sample rate (Timer0 reload 65536-1254)
SUBTICK = 280896 / 4 / 16777216   # seconds per sequencer sub-tick (4 per frame)
WAVE_LEN = 256
NYQ_SAFE = 0.45 * RATE            # keep harmonics below this to limit aliasing

# ------------------------------------------------------------------ MIDI parsing
def read_vlq(d, i):
    v = 0
    while True:
        b = d[i]; i += 1
        v = (v << 7) | (b & 0x7F)
        if not b & 0x80:
            return v, i

def parse_midi(path):
    d = open(path, 'rb').read()
    if d[:4] == b'RIFF':                      # RMID wrapper
        k = d.find(b'MThd')
        d = d[k:]
    if d[:4] != b'MThd':
        raise ValueError('%s: not a MIDI file' % path)
    hlen, fmt, ntrk, div = struct.unpack('>IHHH', d[4:14])
    if div & 0x8000:
        raise ValueError('%s: SMPTE time division not supported' % path)
    pos = 8 + hlen
    events = []   # (tick, seq, kind, data...)
    seq = 0
    for _ in range(ntrk):
        while d[pos:pos + 4] != b'MTrk':
            if pos + 8 > len(d):
                break
            pos += 8 + struct.unpack('>I', d[pos + 4:pos + 8])[0]
        if pos + 8 > len(d):
            break
        tlen = struct.unpack('>I', d[pos + 4:pos + 8])[0]
        i, end = pos + 8, min(len(d), pos + 8 + tlen)
        pos = end
        tick, status = 0, 0
        while i < end:
            delta, i = read_vlq(d, i)
            tick += delta
            b = d[i]
            if b & 0x80:
                status = b; i += 1
            elif status == 0:
                break
            if status == 0xFF:
                typ = d[i]; i += 1
                ln, i = read_vlq(d, i)
                data = d[i:i + ln]; i += ln
                events.append((tick, seq, 'meta', typ, data)); seq += 1
                if typ == 0x2F:
                    break
                status = 0
            elif status in (0xF0, 0xF7):
                ln, i = read_vlq(d, i)
                i += ln
                status = 0
            else:
                hi, ch = status & 0xF0, status & 0x0F
                if hi in (0xC0, 0xD0):
                    a = d[i]; i += 1; bb = 0
                else:
                    a, bb = d[i], d[i + 1]; i += 2
                events.append((tick, seq, 'ch', hi, ch, a, bb)); seq += 1
    events.sort(key=lambda e: (e[0], e[1]))
    return div, events

def tick_to_sec_fn(div, events):
    tempos = [(0, 500000)]
    for e in events:
        if e[2] == 'meta' and e[3] == 0x51 and len(e[4]) == 3:
            t = int.from_bytes(e[4], 'big')
            if tempos[-1][0] == e[0]:
                tempos[-1] = (e[0], t)
            else:
                tempos.append((e[0], t))
    segs, sec = [], 0.0
    for k, (t0, us) in enumerate(tempos):
        segs.append((t0, sec, us))
        if k + 1 < len(tempos):
            sec += (tempos[k + 1][0] - t0) * us / 1e6 / div
    def f(tick):
        lo = 0
        for k, s in enumerate(segs):
            if s[0] <= tick:
                lo = k
        t0, s0, us = segs[lo]
        return s0 + (tick - t0) * us / 1e6 / div
    return f, tempos

# ------------------------------------------------------------------ instruments
# id: (name, harmonics, attack s, decay tau s, sustain 0..1, release tau s, vib cents, vib Hz)
INSTR = [
    ('piano',   [1, .6, .35, .28, .18, .12, .08, .06, .04, .03, .02], .003, 1.1, 0.0, .12, 0, 0),
    ('mallet',  [1, 0, 0, .35, 0, 0, 0, 0, 0, .12], .002, .45, 0.0, .25, 0, 0),
    ('organ',   [1, .8, .5, .3, 0, .2, 0, .15, 0, 0, 0, .08], .012, .5, 1.0, .05, 0, 0),
    ('pluck',   [1, .7, .5, .42, .3, .25, .2, .15, .1, .08, .05, .04], .002, .5, 0.0, .15, 0, 0),
    ('harp',    [1, .42, .2, .1, .06, .03], .002, .9, 0.0, .45, 0, 0),
    ('bass',    [1, .55, .3, .15, .08, .04], .004, .7, .45, .08, 0, 0),
    ('strings', [1 / n for n in range(1, 17)], .11, 1.0, .85, .30, 12, 5.5),
    ('choir',   [1, .45, .55, .2, .12, .08, .04], .22, 2.0, .9, .45, 9, 4.8),
    ('brass',   [1, .85, .7, .55, .45, .35, .25, .2, .14, .1, .07], .035, .45, .8, .12, 6, 5.0),
    ('reed',    [1, .05, .6, .05, .4, .05, .25, .03, .15, .02, .08], .03, .4, .85, .10, 8, 5.2),
    ('flute',   [1, .22, .07, .03], .05, .4, .9, .12, 14, 5.0),
    ('lead',    [1, 0, 1 / 3, 0, 1 / 5, 0, 1 / 7, 0, 1 / 9, 0, 1 / 11], .01, .4, .8, .10, 8, 5.5),
    ('pad',     [1, .3, .35, .12, .1, .05], .30, 2.5, .9, .60, 6, 4.0),
    ('timpani', [1, .45, .15, .06], .003, .7, 0.0, .50, 0, 0),
    ('bell',    [1, 0, .5, 0, 0, .3, 0, 0, .1], .002, 1.6, 0.0, .6, 0, 0),
]
IDX = {n[0]: i for i, n in enumerate(INSTR)}

def gm_to_instr(p):
    if p <= 7: return IDX['piano']
    if p == 14: return IDX['bell']
    if p <= 15: return IDX['mallet']
    if p <= 23: return IDX['organ']
    if p <= 31: return IDX['pluck']
    if p <= 39: return IDX['bass']
    if p == 45: return IDX['pluck']          # pizzicato
    if p == 46: return IDX['harp']
    if p == 47: return IDX['timpani']
    if p <= 51: return IDX['strings']
    if p <= 55: return IDX['choir']
    if p <= 63: return IDX['brass']
    if p <= 71: return IDX['reed']
    if p <= 79: return IDX['flute']
    if p <= 87: return IDX['lead']
    if p <= 103: return IDX['pad']
    if p in (104, 105, 106, 107): return IDX['pluck']
    if p == 108: return IDX['mallet']
    if p in (109, 111): return IDX['reed']
    if p == 110: return IDX['strings']
    if p in (112, 114): return IDX['mallet']
    if p in (116, 117, 118): return IDX['timpani']
    if p <= 119: return IDX['mallet']
    return None                               # sound effects: dropped

# wave bands: the table used for a note is the one whose band contains it, so high notes
# use tables with fewer harmonics (less aliasing at 13.4 kHz)
BAND_TOP = [47, 59, 71, 83, 95, 127]

def note_freq(n):
    return 440.0 * 2 ** ((n - 69) / 12)

def make_wave(harm, fmax):
    rng = random.Random(len(harm))
    phases = [0] + [rng.random() * 0.3 for _ in harm[1:]]
    w = [0.0] * WAVE_LEN
    for k, a in enumerate(harm, 1):
        if a == 0 or k * fmax > NYQ_SAFE and k > 1:
            continue
        for i in range(WAVE_LEN):
            w[i] += a * math.sin(2 * math.pi * (k * i / WAVE_LEN + phases[k - 1]))
    peak = max(abs(x) for x in w) or 1
    return [max(-127, min(127, int(round(x / peak * 120)))) for x in w]

# ------------------------------------------------------------------ drums (one-shot samples)
def gen_drums():
    rng = random.Random(1234)
    R = RATE
    def env(i, n, tau): return math.exp(-i / (tau * R))
    samples = {}
    # kick: pitch-swept sine
    n = int(.30 * R); ph = 0; s = []
    for i in range(n):
        f = 45 + 110 * math.exp(-i / (.03 * R)); ph += 2 * math.pi * f / R
        s.append(math.sin(ph) * env(i, n, .09) + (rng.uniform(-1, 1) * .3 * env(i, n, .004)))
    samples['kick'] = s
    # snare: tone + noise
    n = int(.22 * R); ph = 0; s = []; lp = 0
    for i in range(n):
        ph += 2 * math.pi * 190 / R
        nz = rng.uniform(-1, 1); lp += .6 * (nz - lp)
        s.append(.45 * math.sin(ph) * env(i, n, .03) + .8 * lp * env(i, n, .06))
    samples['snare'] = s
    # closed / open hat: high-passed noise
    for name, dur, tau in (('hatc', .06, .015), ('hato', .35, .11), ('crash', .9, .35)):
        n = int(dur * R); s = []; prev = 0
        for i in range(n):
            nz = rng.uniform(-1, 1); hp = nz - prev; prev = nz
            s.append(hp * .6 * env(i, n, tau))
        samples[name] = s
    # tom: sine with slight sweep
    n = int(.35 * R); ph = 0; s = []
    for i in range(n):
        f = 110 + 50 * math.exp(-i / (.05 * R)); ph += 2 * math.pi * f / R
        s.append(math.sin(ph) * env(i, n, .12))
    samples['tom'] = s
    # clap: noise bursts
    n = int(.18 * R); s = []
    for i in range(n):
        t = i / R
        a = sum(math.exp(-(t - o) / .008) for o in (0, .01, .02) if t >= o) + (.5 * math.exp(-(t - .02) / .06) if t > .02 else 0)
        s.append(rng.uniform(-1, 1) * min(1, a) * .8)
    samples['clap'] = s
    # click (claves / woodblock / rim): short resonant ping
    n = int(.08 * R); s = []
    for i in range(n):
        s.append(math.sin(2 * math.pi * 1800 * i / R) * env(i, n, .012))
    samples['click'] = s
    # cowbell: two detuned squares-ish
    n = int(.25 * R); s = []
    for i in range(n):
        t = i / R
        v = (math.sin(2 * math.pi * 545 * t) + math.sin(2 * math.pi * 815 * t)) * .5
        s.append(v * env(i, n, .07))
    samples['cowbell'] = s
    out = {}
    for k, s in samples.items():
        peak = max(abs(x) for x in s) or 1
        out[k] = [max(-127, min(127, int(round(x / peak * 124)))) for x in s] + [0]
    return out

# GM drum note -> (sample, playback rate, gain 0..1)
def drum_map():
    m = {}
    for n in (35, 36): m[n] = ('kick', 1.0, 1.0)
    m[37] = ('click', .7, .6)
    for n in (38, 40): m[n] = ('snare', 1.0 if n == 38 else 1.1, .85)
    m[39] = ('clap', 1.0, .7)
    for n, r in ((41, .7), (43, .8), (45, .9), (47, 1.0), (48, 1.15), (50, 1.3)): m[n] = ('tom', r, .9)
    for n in (42, 44): m[n] = ('hatc', 1.0, .45)
    m[46] = ('hato', 1.0, .45)
    for n in (49, 52, 55, 57): m[n] = ('crash', 1.0, .55)
    for n in (51, 53, 59): m[n] = ('hato', .7, .35)
    m[54] = ('hatc', 1.4, .5)
    m[56] = ('cowbell', 1.0, .5)
    for n, r in ((60, 2.1), (61, 1.8), (62, 1.6), (63, 1.45), (64, 1.25), (65, 1.1), (66, .95)): m[n] = ('tom', r, .65)
    for n in (69, 70, 82): m[n] = ('hatc', 1.2, .35)
    for n in (75, 76, 77): m[n] = ('click', 1.0 if n == 75 else .8, .5)
    return m

# ------------------------------------------------------------------ song conversion
def find_marker(events, names):
    for e in events:
        if e[2] == 'meta' and e[3] in (0x06, 0x07):
            if e[4].decode('latin1').strip().lower() in names:
                return e[0]
    return None

def convert_song(path):
    div, events = parse_midi(path)
    tsec, tempos = tick_to_sec_fn(div, events)
    to_sub = lambda tick: int(round(tsec(tick) / SUBTICK))
    prog = [0] * 16
    vol = [100] * 16
    expr = [127] * 16
    sus = [False] * 16
    active = {}          # (ch, note) -> list of (start_tick, vel, prog, vol, expr)
    held = {ch: [] for ch in range(16)}   # notes released while the pedal is down
    notes = []           # (start_tick, end_tick, ch, prog, note, vel, vol, expr)
    num, den = 4, 4
    last_tick = 0
    bends = 0
    def end_note(ch, n, tick):
        lst = active.get((ch, n))
        if not lst:
            return
        st, v, p, vo, ex = lst.pop(0)
        notes.append((st, tick, ch, p, n, v, vo, ex))
    for e in events:
        tick = e[0]
        last_tick = max(last_tick, tick)
        if e[2] == 'meta':
            if e[3] == 0x58 and len(e[4]) >= 2 and e[0] == 0:
                num, den = e[4][0], 2 ** e[4][1]
            continue
        _, _, _, hi, ch, a, b = e
        if hi == 0x90 and b > 0:
            if (ch, a) in active and active[(ch, a)]:
                end_note(ch, a, tick)              # retrigger: end the previous one
            active.setdefault((ch, a), []).append((tick, b, prog[ch], vol[ch], expr[ch]))
        elif hi == 0x80 or (hi == 0x90 and b == 0):
            if sus[ch]:
                held[ch].append(a)
            else:
                end_note(ch, a, tick)
        elif hi == 0xC0:
            prog[ch] = a
        elif hi == 0xB0:
            if a == 7: vol[ch] = b
            elif a == 11: expr[ch] = b
            elif a == 64:
                sus[ch] = b >= 64
                if not sus[ch]:
                    for n in held[ch]:
                        end_note(ch, n, tick)
                    held[ch] = []
            elif a in (120, 123):
                for (c, n) in list(active):
                    if c == ch:
                        while active[(c, n)]:
                            end_note(c, n, tick)
        elif hi == 0xE0:
            bends += 1
    for (c, n), lst in list(active.items()):
        while lst:
            end_note(c, n, last_tick)
    if not notes:
        raise ValueError('%s: no notes' % path)
    notes.sort()
    # loop points
    ls = find_marker(events, ('loopstart', 'loop start', 'loop_start', 'start'))
    le = find_marker(events, ('loopend', 'loop end', 'loop_end'))
    bar_ticks = div * 4 * num // den
    if le is None:
        end_tick = max(n[1] for n in notes)
        end_tick = -(-end_tick // bar_ticks) * bar_ticks
    else:
        end_tick = le
    loop_tick = ls or 0
    # build note list in sub-ticks
    out = []
    dropped = 0
    for st, en, ch, p, n, v, vo, ex in notes:
        if st >= end_tick:
            continue
        if ch == 9:
            inst, key = 0x40, n
            if n not in DRUMS:
                dropped += 1
                continue
        else:
            inst = gm_to_instr(p)
            if inst is None:
                dropped += 1
                continue
            key = n
        amp = (v / 127) ** 2 * (vo / 127) ** 2 * (ex / 127) ** 2
        if ch == 9:
            amp *= DRUMS[n][2]
        s0 = to_sub(st)
        dur = max(1, to_sub(min(en, end_tick + bar_ticks)) - s0)
        out.append([s0, inst, key, amp, dur])
    # loudness normalisation: aim the 90th percentile of summed amplitude at a fixed level
    total = to_sub(end_tick)
    level = [0.0] * (total + 1)
    for s0, inst, key, amp, dur in out:
        for t in range(s0, min(total, s0 + min(dur, 120)) + 1, 4):
            level[t] += amp
    lv = sorted(level[::4])
    p90 = lv[int(len(lv) * .9)] or 1
    gain = min(4.0, max(.4, 1.6 / p90))
    stream = bytearray()
    loop_off, loop_sub = None, to_sub(loop_tick)
    t = 0
    maxpoly = 0
    for s0, inst, key, amp, dur in out:
        if loop_off is None and s0 >= loop_sub:
            loop_off = len(stream)
            # the first event after the loop point must carry its delta from the loop point
        a = max(1, min(127, int(round(amp * gain * 127))))
        delta = s0 - t
        t = s0
        stream += vlq(delta) + bytes([inst, key, a]) + vlq(min(dur, 0x3FFF))
    if loop_off is None:
        loop_off = len(stream)
    stream += vlq(max(0, total - t)) + b'\xff'
    # polyphony estimate
    ev = sorted([(s0, 1) for s0, _, _, _, _ in out] + [(s0 + d, -1) for s0, _, _, _, d in out])
    cur = 0
    for _, dlt in ev:
        cur += dlt; maxpoly = max(maxpoly, cur)
    # delta of the first looped event must be measured from loop point: when jumping back
    # the player restarts its clock at loop_sub, so store the absolute delta fix-up here
    first_loop_delta = None
    for s0, *_ in out:
        if s0 >= loop_sub:
            first_loop_delta = s0 - loop_sub
            break
    info = dict(notes=len(out), dropped=dropped, poly=maxpoly, secs=total * SUBTICK, gain=gain,
                bends=bends, tempo=60e6 / tempos[0][1], bytes=len(stream))
    return bytes(stream), loop_off, first_loop_delta or 0, info

def vlq(n):
    out = [n & 0x7F]
    n >>= 7
    while n:
        out.append(0x80 | (n & 0x7F)); n >>= 7
    return bytes(reversed(out))

DRUMS = drum_map()

# ------------------------------------------------------------------ C output
def carr(name, data, ctype='u8', per=24):
    lines = []
    for i in range(0, len(data), per):
        lines.append('    ' + ','.join(str(x) for x in data[i:i + per]) + ',')
    return 'const %s %s[%d] = {\n%s\n};\n' % (ctype, name, len(data), '\n'.join(lines))

def main():
    manifest, outdir = sys.argv[1], sys.argv[2]
    mdir = os.path.dirname(os.path.abspath(manifest))
    tracks = []
    for line in open(manifest):
        line = line.split('#', 1)[0].strip()
        if not line:
            continue
        parts = [p.strip() for p in line.split('|')]
        while len(parts) < 3:
            parts.append('')
        regions = [r.strip() for r in parts[2].split(',') if r.strip()]
        tracks.append((parts[0].upper(), parts[1], regions))
    c, h = [], []
    h.append('// Generated by tools/midi2gba.py from music/tracks.txt -- do not edit.')
    h.append('#pragma once')
    h.append('#include "gba.h"')
    h.append('enum {')
    for k, (tid, _, _) in enumerate(tracks):
        h.append('    MUS_%s = %d,' % (tid, k))
    h.append('    MUS_COUNT = %d,' % len(tracks))
    h.append('    MUS_NONE = -1,')
    h.append('};')
    h.append('#define MUS_SUBTICKS 4')
    h.append('#define MUS_WAVE_BANDS %d' % len(BAND_TOP))
    h.append('#define MUS_INSTR_COUNT %d' % len(INSTR))
    h.append('#define MUS_DRUM 0x40')
    h.append('typedef struct { const u8 *data; u16 len, loop; u16 loop_delta; } MusicTrack;')
    h.append('typedef struct { u16 atk, dec, sus, rel; u8 vib_depth, vib_rate; u16 wave; } MusicInstr;')
    h.append('typedef struct { const s8 *data; u32 len; u16 step; u8 gain; } MusicDrum;')
    h.append('typedef struct { const char *name; s8 track; } MusicRegion;')
    h.append('extern const MusicTrack music_tracks[MUS_COUNT];')
    h.append('extern const MusicInstr music_instr[MUS_INSTR_COUNT];')
    h.append('extern const s8 music_waves[][256];')
    h.append('extern const u8 music_band[128];')
    h.append('extern const u32 music_step[128];')
    h.append('extern const MusicDrum music_drums[128];')
    h.append('extern const s8 music_softclip[1024];')
    h.append('extern const s8 music_sine64[64];')
    h.append('extern const MusicRegion music_regions[];')
    h.append('extern const int music_region_count;')
    c.append('// Generated by tools/midi2gba.py from music/tracks.txt -- do not edit.')
    c.append('#include "music_data.h"')
    report = []
    for k, (tid, fname, regions) in enumerate(tracks):
        data, loop, loop_delta, info = convert_song(os.path.join(mdir, fname))
        c.append(carr('mus_%s' % tid.lower(), list(data)))
        report.append('  %-11s %-16s %4d notes %5d B  poly %2d  %5.1fs  %5.1f bpm  gain %.2f%s%s' % (
            tid, fname, info['notes'], info['bytes'], info['poly'], info['secs'], info['tempo'], info['gain'],
            '  (%d dropped)' % info['dropped'] if info['dropped'] else '',
            '  (%d pitch bends ignored)' % info['bends'] if info['bends'] else ''))
    c.append('const MusicTrack music_tracks[MUS_COUNT] = {')
    for tid, _, _ in tracks:
        c.append('    {mus_%s, sizeof mus_%s, 0, 0},' % (tid.lower(), tid.lower()))
    c.append('};')
    # patch loop offsets (computed above; recompute cheaply to keep code simple)
    loops = []
    for tid, fname, _ in tracks:
        _, loop, ld, _ = convert_song(os.path.join(mdir, fname))
        loops.append((loop, ld))
    c[-1 - len(tracks):-1] = ['    {mus_%s, sizeof mus_%s, %d, %d},' % (t[0].lower(), t[0].lower(), l, ld)
                              for t, (l, ld) in zip(tracks, loops)]
    # instruments + waves
    waves = []
    sub_rate = 1 / SUBTICK
    c.append('const MusicInstr music_instr[MUS_INSTR_COUNT] = {')
    for name, harm, atk, dec, sus, rel, vc, vh in INSTR:
        base = len(waves)                     # one block of MUS_WAVE_BANDS tables per instrument
        for top in BAND_TOP:
            waves.append(make_wave(harm, note_freq(top)))
        a_inc = min(32767, int(32767 / max(1, atk * sub_rate)))
        d_mul = int(65536 * math.exp(-1 / (dec * sub_rate)))
        r_mul = int(65536 * math.exp(-1 / (rel * sub_rate)))
        depth = int(round(vc * math.log(2) / 1200 * 4096 * 2))   # step += (step>>12)*depth*sin/256
        rate = int(round(vh * 256 / sub_rate))
        c.append('    {%d, %d, %d, %d, %d, %d, %d},  // %s' % (a_inc, d_mul, int(sus * 32767), r_mul, depth, rate, base, name))
    c.append('};')
    c.append('const s8 music_waves[][256] = {')
    for w in waves:
        c.append('    {' + ','.join(map(str, w)) + '},')
    c.append('};')
    band = [next(b for b, top in enumerate(BAND_TOP) if n <= top) for n in range(128)]
    c.append(carr('music_band', band))
    steps = [min(0xFFFFFFFF, int(note_freq(n) / RATE * 2 ** 32)) for n in range(128)]
    c.append(carr('music_step', steps, 'u32', 8))
    # drums
    ds = gen_drums()
    for k, s in ds.items():
        c.append(carr('drum_%s' % k, s, 's8'))
    c.append('const MusicDrum music_drums[128] = {')
    for n in range(128):
        if n in DRUMS:
            name, r, g = DRUMS[n]
            c.append('    [%d] = {drum_%s, %d, %d, %d},' % (n, name, len(ds[name]) - 1, int(r * 4096), int(g * 255)))
    c.append('};')
    # soft clip: linear up to ~70% then tanh knee; input index = acc>>shift + 512
    sc = []
    for i in range(1024):
        x = (i - 512) / 512 * 1.6
        y = math.tanh(x) if abs(x) > .7 else x * (math.tanh(.7) / .7)
        sc.append(max(-127, min(127, int(round(y * 127)))))
    c.append(carr('music_softclip', sc, 's8'))
    c.append(carr('music_sine64', [int(round(127 * math.sin(2 * math.pi * i / 64))) for i in range(64)], 's8'))
    c.append('const MusicRegion music_regions[] = {')
    nreg = 0
    for k, (tid, _, regions) in enumerate(tracks):
        for r in regions:
            c.append('    {"%s", MUS_%s},' % (r.replace('"', ''), tid)); nreg += 1
    c.append('    {0, -1},')
    c.append('};')
    c.append('const int music_region_count = %d;' % nreg)
    os.makedirs(outdir, exist_ok=True)
    open(os.path.join(outdir, 'music_data.c'), 'w').write('\n'.join(c) + '\n')
    open(os.path.join(outdir, 'music_data.h'), 'w').write('\n'.join(h) + '\n')
    print('midi2gba: %d tracks, %d waves, %d drum samples' % (len(tracks), len(waves), len(ds)))
    print('\n'.join(report))

if __name__ == '__main__':
    main()
