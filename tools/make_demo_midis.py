#!/usr/bin/env python3
"""Compose the ORIGINAL placeholder soundtrack (one MIDI per music slot) into music/.

These pieces are generated from seeded chord progressions and melody rules written for this
project; they are not transcriptions of any existing work. Replace any music/<slot>.mid with
your own MIDI (see music/SOURCES.md) and rebuild -- tools/midi2gba.py picks it up.

usage: make_demo_midis.py [outdir]   (default: music/)
"""
import os, random, struct, sys

PPQ = 96

# ---------------------------------------------------------------- SMF writer
def vlq(n):
    out = [n & 0x7F]
    n >>= 7
    while n:
        out.append(0x80 | (n & 0x7F))
        n >>= 7
    return bytes(reversed(out))

def track_chunk(events):
    """events: list of (tick, order, bytes)"""
    events.sort(key=lambda e: (e[0], e[1]))
    data, last = bytearray(), 0
    for t, _, b in events:
        data += vlq(t - last) + b
        last = t
    data += vlq(0) + b'\xff\x2f\x00'
    return b'MTrk' + struct.pack('>I', len(data)) + bytes(data)

def write_midi(path, bpm, num, parts, length_beats, title):
    tempo = int(60000000 / bpm)
    meta = [(0, 0, b'\xff\x03' + vlq(len(title)) + title.encode()),
            (0, 0, b'\xff\x51\x03' + tempo.to_bytes(3, 'big')),
            (0, 0, bytes([0xFF, 0x58, 4, num, 2, 24, 8])),
            (int(length_beats * PPQ), 9, b'\xff\x06\x07loopEnd')]
    chunks = [track_chunk(meta)]
    for ch, prog, vol, notes in parts:
        ev = [(0, 0, bytes([0xC0 | ch, prog])), (0, 0, bytes([0xB0 | ch, 7, vol]))]
        for start, dur, note, vel in notes:
            t0 = int(round(start * PPQ))
            t1 = max(t0 + 1, int(round((start + dur) * PPQ)) - 2)
            ev.append((t0, 2, bytes([0x90 | ch, note, vel])))
            ev.append((t1, 1, bytes([0x80 | ch, note, 0])))
        chunks.append(track_chunk(ev))
    with open(path, 'wb') as f:
        f.write(b'MThd' + struct.pack('>IHHH', 6, 1, len(chunks), PPQ) + b''.join(chunks))

# ---------------------------------------------------------------- harmony helpers
MODES = {
    'major': [0, 2, 4, 5, 7, 9, 11],
    'minor': [0, 2, 3, 5, 7, 8, 10],
    'dorian': [0, 2, 3, 5, 7, 9, 10],
    'phrygdom': [0, 1, 4, 5, 7, 8, 10],
    'harmminor': [0, 2, 3, 5, 7, 8, 11],
}

class Key:
    def __init__(self, tonic, mode):
        self.tonic, self.scale = tonic, MODES[mode]
    def deg(self, d, octave=0):
        """scale degree d (0-based, may exceed 6 or be negative) -> semitone offset"""
        o, i = divmod(d, 7)
        return self.tonic + 12 * (octave + o) + self.scale[i]
    def chord(self, root, size=3):
        return [root + 2 * k for k in range(size)]   # stacked thirds as scale degrees
    def nearest(self, pitch, degrees):
        best = None
        for o in range(-2, 9):
            for d in degrees:
                p = self.deg(d, o)
                if best is None or abs(p - pitch) < abs(best - pitch):
                    best = p
        return best
    def scale_step(self, pitch, steps):
        allp = sorted(self.deg(d, o) for o in range(-1, 10) for d in range(7))
        i = min(range(len(allp)), key=lambda k: abs(allp[k] - pitch))
        return allp[max(0, min(len(allp) - 1, i + steps))]

RHYTHMS4 = [[1, 1, 1, 1], [1.5, .5, 1, 1], [1, .5, .5, 2], [2, 1, 1], [.5, .5, 1, 2], [1, 1, 2],
            [1.5, .5, 2], [.5, .5, .5, .5, 2], [3, 1], [1, .5, .5, 1, 1]]
RHYTHMS3 = [[1, 1, 1], [2, 1], [1.5, .5, 1], [1, .5, .5, 1], [3], [.5, .5, 1, 1], [1, 2]]

def melody(rng, key, prog, beats, lo, hi, vel=96, cadence_every=4, start=None):
    """Melody over a list of chord roots (one per bar). Bars repeat in an A A' B A'' shape:
    2-bar motifs are reused with pitch adjustment so the tune has recognisable phrases."""
    rhy = RHYTHMS4 if beats == 4 else RHYTHMS3
    notes, pitch = [], start if start is not None else key.deg(0, 5)
    motifs = {}
    for bar, root in enumerate(prog):
        chord = key.chord(root)
        phrase_pos = bar % 8
        motif_id = {0: 'a', 1: 'b', 2: 'c', 3: 'd', 4: 'a', 5: 'b', 6: 'e', 7: 'f'}[phrase_pos]
        last_of_section = (bar % cadence_every) == cadence_every - 1
        if last_of_section:
            pattern = [beats - 1, 1] if (bar % 8) != 7 else [beats]
        elif motif_id in motifs:
            pattern = motifs[motif_id][0]
        else:
            pattern = rng.choice(rhy)
        contour = motifs[motif_id][1] if motif_id in motifs and not last_of_section else None
        if contour is None:
            contour = [rng.choice([-2, -1, -1, 1, 1, 2, 0, 3, -3]) for _ in pattern]
            if not last_of_section:
                motifs[motif_id] = (pattern, contour)
        t = bar * beats
        for i, dur in enumerate(pattern):
            strong = (t % beats) == 0 or dur >= 2
            if last_of_section and i == len(pattern) - 1:
                target = key.nearest(pitch, [0, 2, 4] if (bar % 8) == 7 else chord)
            elif strong:
                target = key.nearest(key.scale_step(pitch, contour[i]), chord)
            else:
                target = key.scale_step(pitch, contour[i])
            while target > hi: target -= 12
            while target < lo: target += 12
            pitch = target
            accent = 10 if (t % beats) == 0 else 0
            notes.append((t, dur, pitch, min(127, vel + accent + rng.randint(-6, 6))))
            t += dur
    return notes

def pad(key, prog, beats, octave, vel, size=3, every=None):
    out = []
    every = every or beats
    for bar, root in enumerate(prog):
        for b in range(0, beats, every):
            for d in key.chord(root, size):
                out.append((bar * beats + b, every, key.deg(d, octave), vel))
    return out

def arp(key, prog, beats, octave, vel, step=.5, pattern=(0, 1, 2, 3, 2, 1)):
    out = []
    for bar, root in enumerate(prog):
        tones = [key.deg(d, octave) for d in key.chord(root)] + [key.deg(root, octave + 1)]
        n = int(beats / step)
        for i in range(n):
            p = tones[pattern[i % len(pattern)] % len(tones)]
            out.append((bar * beats + i * step, step * 1.6, p, vel - (0 if i % 2 == 0 else 12)))
    return out

def bass(key, prog, beats, octave, vel, style='root'):
    out = []
    for bar, root in enumerate(prog):
        r = key.deg(root, octave)
        t = bar * beats
        if style == 'root':
            out.append((t, beats, r, vel))
        elif style == 'half':
            out += [(t, 2, r, vel), (t + 2, 2, key.deg(root + 4, octave), vel - 8)]
        elif style == 'waltz':
            out.append((t, 1, r, vel))
            for d in key.chord(root)[1:]:
                out += [(t + 1, 1, key.deg(d, octave + 1), vel - 30), (t + 2, 1, key.deg(d, octave + 1), vel - 34)]
        elif style == 'march':
            out += [(t, 1, r, vel), (t + 1, 1, key.deg(root + 4, octave), vel - 10),
                    (t + 2, 1, r, vel - 4), (t + 3, 1, key.deg(root + 4, octave - 1), vel - 10)]
        elif style == 'drone':
            out.append((t, beats, key.deg(0, octave), vel))
        elif style == 'pulse':
            out += [(t + b, .9, r, vel - (0 if b == 0 else 14)) for b in range(beats)]
    return out

def drums(pattern, bars, beats, vel=100):
    """pattern: dict note -> list of beat offsets within one bar"""
    out = []
    for bar in range(bars):
        for note, hits in pattern.items():
            for h in hits:
                out.append((bar * beats + h, .25, note, vel if h == int(h) else vel - 25))
    return out

def song(path, title, seed, key, beats, bpm, prog, parts_fn):
    rng = random.Random(seed)
    parts = parts_fn(rng, key, prog)
    write_midi(path, bpm, beats, parts, len(prog) * beats, title)

# ---------------------------------------------------------------- the eight slot pieces
def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), '..', 'music')
    os.makedirs(out, exist_ok=True)
    P = lambda *degs: list(degs)

    # Title: broad C major hymn for brass and strings
    prog = P(0, 4, 5, 3, 0, 3, 4, 4, 5, 2, 3, 0, 3, 4, 0, 0)
    song(os.path.join(out, 'title.mid'), 'Title (original)', 11, Key(0, 'major'), 4, 92, prog,
         lambda r, k, p: [(0, 61, 100, melody(r, k, p, 4, 60, 81, 92)),
                          (1, 48, 80, pad(k, p, 4, 4, 70)),
                          (2, 46, 70, arp(k, p, 4, 4, 80, .5)),
                          (3, 43, 100, bass(k, p, 4, 2, 96, 'half')),
                          (4, 47, 90, [(b * 4, 1, k.deg(p[b], 2), 100) for b in range(len(p)) if b % 2 == 0])])
    # Lumbridge: pastoral G major waltz, flute over harp
    prog = P(0, 3, 4, 0, 5, 3, 1, 4, 0, 3, 4, 5, 3, 4, 0, 0)
    song(os.path.join(out, 'lumbridge.mid'), 'Lumbridge (original)', 23, Key(7, 'major'), 3, 104, prog,
         lambda r, k, p: [(0, 73, 105, melody(r, k, p, 3, 67, 86, 90)),
                          (1, 46, 85, arp(k, p, 3, 4, 78, .5, (0, 1, 2, 3, 2, 1))),
                          (2, 49, 60, pad(k, p, 3, 3, 60)),
                          (3, 32, 95, bass(k, p, 3, 2, 92, 'root'))])
    # Varrock: D dorian march with snare
    prog = P(0, 0, 6, 0, 3, 3, 4, 4, 0, 6, 5, 4, 3, 6, 0, 0)
    song(os.path.join(out, 'varrock.mid'), 'Varrock (original)', 37, Key(2, 'dorian'), 4, 112, prog,
         lambda r, k, p: [(0, 56, 95, melody(r, k, p, 4, 62, 81, 90)),
                          (1, 48, 75, pad(k, p, 4, 3, 64, 3, 2)),
                          (2, 60, 70, pad(k, p, 4, 4, 58, 2, 4)),
                          (3, 33, 100, bass(k, p, 4, 2, 96, 'march')),
                          (9, 0, 90, drums({36: [0, 2], 38: [1, 3, 3.5], 42: [0, .5, 1, 1.5, 2, 2.5, 3, 3.5]}, len(p), 4, 90))])
    # Falador: bright F major fanfare
    prog = P(0, 3, 4, 0, 0, 5, 1, 4, 3, 4, 2, 5, 1, 4, 0, 0)
    song(os.path.join(out, 'falador.mid'), 'Falador (original)', 41, Key(5, 'major'), 4, 124, prog,
         lambda r, k, p: [(0, 56, 105, melody(r, k, p, 4, 65, 84, 96)),
                          (1, 60, 80, pad(k, p, 4, 3, 66, 3, 2)),
                          (2, 48, 70, arp(k, p, 4, 4, 70, .5, (0, 2, 1, 3))),
                          (3, 43, 100, bass(k, p, 4, 2, 96, 'half')),
                          (4, 47, 85, [(b * 4 + o, .5, k.deg(0 if o == 0 else 4, 2), 96) for b in range(len(p)) for o in (0, 3.5)]),
                          (9, 0, 70, drums({49: [0], 42: [1, 2, 3]}, len(p), 4, 70))])
    # Draynor: misty A minor, harp and pan flute over a warm pad
    prog = P(0, 5, 2, 6, 0, 3, 4, 4, 5, 6, 0, 2, 5, 3, 4, 0)
    song(os.path.join(out, 'draynor.mid'), 'Draynor (original)', 53, Key(9, 'minor'), 4, 80, prog,
         lambda r, k, p: [(0, 75, 100, melody(r, k, p, 4, 64, 84, 88)),
                          (1, 46, 90, arp(k, p, 4, 3, 80, .5, (0, 1, 2, 3, 1, 2, 3, 2))),
                          (2, 89, 70, pad(k, p, 4, 3, 58, 3)),
                          (3, 32, 90, bass(k, p, 4, 2, 90, 'root'))])
    # Al Kharid: E phrygian-dominant, plucked lead, drone and hand drums
    prog = P(0, 1, 0, 6, 0, 1, 2, 0, 3, 2, 1, 0, 3, 1, 0, 0)
    song(os.path.join(out, 'alkharid.mid'), 'Al Kharid (original)', 67, Key(4, 'phrygdom'), 4, 104, prog,
         lambda r, k, p: [(0, 104, 100, melody(r, k, p, 4, 64, 83, 96)),
                          (1, 24, 80, arp(k, p, 4, 3, 72, 1, (0, 2, 1, 2))),
                          (2, 49, 55, [(b * 4, 4, k.deg(d, 3), 56) for b in range(len(p)) for d in (0, 4)]),
                          (3, 32, 95, bass(k, p, 4, 2, 92, 'drone')),
                          (9, 0, 90, drums({60: [0, 1.5, 2.5], 61: [1, 3, 3.5], 54: [.5, 1.5, 2.5, 3.5]}, len(p), 4, 92))])
    # Edgeville: wistful E minor piano waltz
    prog = P(0, 5, 3, 4, 0, 5, 1, 4, 2, 5, 3, 0, 5, 3, 4, 0)
    song(os.path.join(out, 'edgeville.mid'), 'Edgeville (original)', 79, Key(4, 'harmminor'), 3, 88, prog,
         lambda r, k, p: [(0, 0, 105, melody(r, k, p, 3, 64, 84, 92)),
                          (1, 0, 80, bass(k, p, 3, 2, 84, 'waltz')),
                          (2, 48, 55, pad(k, p, 3, 3, 52, 3)),
                          (3, 42, 85, bass(k, p, 3, 2, 80, 'root'))])
    # Wilderness: dark C minor, low strings, choir, toms
    prog = P(0, 0, 5, 5, 3, 3, 4, 4, 0, 5, 6, 4, 0, 3, 4, 0)
    song(os.path.join(out, 'wilderness.mid'), 'Wilderness (original)', 97, Key(0, 'harmminor'), 4, 86, prog,
         lambda r, k, p: [(0, 60, 100, melody(r, k, p, 4, 55, 76, 90)),
                          (1, 48, 85, bass(k, p, 4, 3, 74, 'pulse')),
                          (2, 52, 70, pad(k, p, 4, 3, 60, 3)),
                          (3, 43, 100, bass(k, p, 4, 1, 96, 'root')),
                          (4, 47, 90, [(b * 4 + o, 1, k.deg(0, 2), 96 - 20 * (o > 0)) for b in range(len(p)) for o in (0, 2.5, 3)]),
                          (9, 0, 80, drums({45: [0, 2], 41: [3, 3.5], 49: [0] if False else []}, len(p), 4, 86))])

if __name__ == '__main__':
    main()
