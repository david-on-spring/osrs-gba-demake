// Sound: a software wavetable synth for music plus PSG sound effects.
//
// Music: Timer0 clocks DirectSound A at 13379 Hz (exactly 224 samples per frame). DMA1 feeds
// FIFO A from a 4-frame ring that the VBlank IRQ (crt0.s) rewinds every 4th frame; the main
// loop keeps the ring two frames ahead of playback. Each frame is mixed in 4 sub-ticks of 56
// samples; the sequencer (note streams from tools/midi2gba.py) and the ADSR envelopes run per
// sub-tick, and voice volume ramps linearly across each sub-tick so there is no zipper noise.
// Voices read 256-sample single-cycle waves (band-limited per octave range) or one-shot drum
// samples; the inner loops are ARM code in IWRAM.
//
// SFX: square-wave blips and jingles on PSG channel 1 and noise on channel 4, mixed with the
// DirectSound output by the hardware.
#include "game.h"

#define FRAME_SAMPLES 224
#define SUB_SAMPLES (FRAME_SAMPLES / MUS_SUBTICKS)
#define RING_FRAMES 4
#define NVOICES 10
#define MIX_SHIFT 9            // accumulator >> MIX_SHIFT indexes the 1024-entry soft clipper

// ---- shared with the VBlank IRQ in crt0.s ----
s8 snd_ring[RING_FRAMES * FRAME_SAMPLES + 32] __attribute__((aligned(4)));
volatile u32 snd_play_frame;

// ---- measurements (read with `dump @snd_stats N` in the harness) ----
struct {
    u16 mix_lines_last, mix_lines_max;  // scanlines spent in sound_update
    u16 frame_drops;                    // frames where the main loop missed a VBlank
    u16 voices_max;                     // most voices sounding at once
    u16 steals;                         // notes that had to take over a sounding voice
    u16 loops;                          // times the current track wrapped to its loop point
    u32 mixed_frames;
    s8 track, want, on, fade;              // player state snapshot
    u32 start_frame, loop_frame;           // mixed_frames when the track started / last looped
    u32 drop_frame;                        // play frame of the most recent frame drop
} snd_stats;

enum { V_OFF, V_ATTACK, V_DECAY, V_RELEASE, V_DRUM };

typedef struct {
    const s8 *data;
    u32 pos, step, base_step, end;      // waves: 8.24 phase into 256 entries; drums: 20.12, end = len<<12
    s32 vol;                            // current mixer volume 0..255
    s32 env;                            // 0..32767
    const MusicInstr *ins;
    u16 hold;                           // sub-ticks until key-off
    u8 stage, inst, note, amp, lfo;
    u8 pad;
} Voice;

static Voice voices[NVOICES];
static s8 voice_wave[NVOICES][256] __attribute__((aligned(4)));   // IWRAM copies of the waves
static s32 acc[SUB_SAMPLES];

typedef struct {
    const MusicTrack *t;
    const u8 *p;
    s32 wait;
} Seq;
static Seq seq;
static s8 cur_track = MUS_NONE, want_track = MUS_NONE;
static u8 music_on = 1, fading;
static s32 gain = 4096;          // track gain, Q12 (fades)
static s32 duck = 4096, duck_target = 4096;
static u16 duck_timer;
static u32 mixed_frame, last_play_frame;

// ------------------------------------------------------------------ mixer inner loops (ARM, IWRAM)
// in mixer.s (ARM, IWRAM)
u32 mix_wave(s32 *acc, const s8 *wave, u32 pos, u32 step, s32 volq8, s32 dvol4, int n);
u32 mix_oneshot(s32 *acc, const s8 *sample, u32 pos, u32 step, s32 volq8, s32 dvol4, int n);

void mix_out(s8 *out, s32 *acc, const s8 *clip_mid, int n);   // mixer.s: soft-clip + clear acc
static s8 softclip[1024] __attribute__((aligned(4)));             // IWRAM copy of music_softclip

// ------------------------------------------------------------------ voices
static void voice_target(Voice *v, s32 *target) {
    s32 t = (v->env * v->amp) >> 14;    // 0..254
    t = (t * gain) >> 12;
    t = (t * duck) >> 12;
    *target = t;
}

static void env_tick(Voice *v) {
    const MusicInstr *I = v->ins;
    switch (v->stage) {
    case V_ATTACK:
        v->env += I->atk;
        if (v->env >= 32767) {
            v->env = 32767;
            v->stage = V_DECAY;
        }
        break;
    case V_DECAY:
        v->env = I->sus + (((v->env - I->sus) * (s32)I->dec) >> 16);
        if (!I->sus && v->env < 160) v->stage = V_OFF;   // ~-46 dB
        break;
    case V_RELEASE:
        v->env = (v->env * (s32)I->rel) >> 16;
        if (v->env < 160) v->stage = V_OFF;
        break;
    }
    if (v->hold && --v->hold == 0 && (v->stage == V_ATTACK || v->stage == V_DECAY)) v->stage = V_RELEASE;
    if (I->vib_depth && v->stage != V_OFF) {
        v->lfo += I->vib_rate;
        s32 s = music_sine64[v->lfo >> 2];
        v->step = v->base_step + (s32)(((v->base_step >> 12) * I->vib_depth * s) >> 8);
    }
}

static Voice *voice_alloc(int inst, int note) {
    Voice *best = 0;
    s32 best_score = 0x7FFFFFFF;
    for (int i = 0; i < NVOICES; i++) {
        Voice *v = &voices[i];
        if (v->stage == V_OFF) return v;
        if (v->inst == inst && v->note == note) return v;   // retrigger the same key
        s32 score = (v->env >> 7) * v->amp + (v->stage == V_RELEASE ? 0 : 0x100000);
        if (score < best_score) {
            best_score = score;
            best = v;
        }
    }
    snd_stats.steals++;
    return best;
}

static void note_on(int inst, int note, int amp, int dur) {
    Voice *v = voice_alloc(inst, note);
    if (inst == MUS_DRUM) {
        const MusicDrum *d = &music_drums[note];
        if (!d->data) return;
        v->data = d->data;
        v->pos = 0;
        v->step = v->base_step = d->step;
        v->end = d->len << 12;
        v->env = 32767;
        v->stage = V_DRUM;
    } else {
        const MusicInstr *I = &music_instr[inst];
        v->ins = I;
        int vi = v - voices;
        dma3_copy32(voice_wave[vi], music_waves[I->wave + music_band[note]], 64);
        v->data = voice_wave[vi];
        v->step = v->base_step = music_step[note];
        v->end = 0;
        v->env = 0;
        v->lfo = 0;
        v->stage = V_ATTACK;
        if (v->vol) v->pos &= 0xFF000000;   // keep phase on retrigger/steal to avoid a jump
        else v->pos = 0;
    }
    v->inst = inst;
    v->note = note;
    v->amp = amp;
    v->hold = dur;
}

static void voices_stop(void) {
    for (int i = 0; i < NVOICES; i++) voices[i].stage = V_OFF;
}

// ------------------------------------------------------------------ sequencer
static u32 vlq(const u8 **pp) {
    const u8 *p = *pp;
    u32 v = 0;
    u8 b;
    do {
        b = *p++;
        v = (v << 7) | (b & 0x7F);
    } while (b & 0x80);
    *pp = p;
    return v;
}

static void seq_start(int t) {
    voices_stop();
    cur_track = t;
    if (t < 0 || t >= MUS_COUNT) {
        seq.p = 0;
        cur_track = MUS_NONE;
        return;
    }
    seq.t = &music_tracks[t];
    seq.p = seq.t->data;
    seq.wait = vlq(&seq.p);
    snd_stats.start_frame = snd_stats.mixed_frames;
    gain = 4096;
    fading = 0;
}

static void seq_tick(void) {
    if (!seq.p) return;
    int guard = 64;
    while (seq.wait <= 0 && guard--) {
        u8 inst = *seq.p++;
        if (inst == 0xFF) {
            seq.p = seq.t->data + seq.t->loop;
            vlq(&seq.p);                   // the stored delta is relative to the pre-loop event
            seq.wait = seq.t->loop_delta;
            snd_stats.loops++;
            snd_stats.loop_frame = snd_stats.mixed_frames;
            continue;
        }
        u8 note = *seq.p++;
        u8 amp = *seq.p++;
        u32 dur = vlq(&seq.p);
        note_on(inst, note, amp, dur);
        seq.wait = vlq(&seq.p);
    }
    seq.wait--;
}

static void control_tick(void) {
    if (fading) {
        gain -= 22;                        // ~0.8 s fade
        if (gain <= 0) {
            gain = 0;
            fading = 0;
            voices_stop();
            seq.p = 0;
            cur_track = MUS_NONE;
            if (music_on && want_track != MUS_NONE) seq_start(want_track);
        }
    }
    if (duck != duck_target) duck += duck < duck_target ? 32 : -64;
    if ((duck_target - duck) * (duck_target - duck) < 64 * 64) duck = duck_target;
}

// ------------------------------------------------------------------ frame mixing
static void mix_frame(s8 *out) {
    int active = 0;
    for (int sub = 0; sub < MUS_SUBTICKS; sub++, out += SUB_SAMPLES) {
        control_tick();
        seq_tick();
        for (int i = 0; i < NVOICES; i++) {
            Voice *v = &voices[i];
            if (v->stage == V_OFF && !v->vol) continue;
            s32 target = 0;
            if (v->stage != V_OFF) {
                if (v->stage != V_DRUM) env_tick(v);
                if (v->stage != V_OFF) voice_target(v, &target);
            }
            s32 dv = ((target - v->vol) * 4681) >> 8;   // per 4 samples, Q8: (d << 8) / 14
            s32 vq = v->vol << 8;
            if (v->end) {
                u32 left = (v->end - v->pos + v->step - 1) / v->step;
                int n = left < SUB_SAMPLES ? (int)left : SUB_SAMPLES;
                v->pos = mix_oneshot(acc, v->data, v->pos, v->step, vq, dv, n);
                if (v->pos >= v->end) {
                    v->stage = V_OFF;
                    v->end = 0;
                    target = 0;
                }
            } else {
                v->pos = mix_wave(acc, v->data, v->pos, v->step, vq, dv, SUB_SAMPLES);
            }
            v->vol = target;
            if (v->stage == V_OFF) v->vol = 0;
            active++;
        }
        mix_out(out, acc, softclip + 512, SUB_SAMPLES);
    }
    active /= MUS_SUBTICKS;
    if (active > snd_stats.voices_max) snd_stats.voices_max = active;
}

// ------------------------------------------------------------------ public API
void sound_init(void) {
    dma3_copy32(softclip, music_softclip, sizeof softclip / 4);
    REG_SOUNDCNT_X = 0x80;
    REG_SOUNDCNT_L = 0xFF77;
    // PSG 100%, DirectSound A 100% to both speakers on Timer0, FIFO A reset
    REG_SOUNDCNT_H = 0x0002 | 0x0004 | 0x0300 | 0x0800;
    REG_SOUND1CNT_L = 0x0008;
    REG_DMA1DAD = 0x040000A0;              // FIFO A; the VBlank IRQ (re)starts DMA1
    REG_TM0CNT_L = 65536 - 1254;
    REG_TM0CNT_H = 0x0080;
}

void music_play(int track) {
    want_track = track;
    if (!music_on) return;
    if (track == cur_track && seq.p) {
        if (fading) {                       // came back to the track that was fading out
            fading = 0;
            gain = 4096;
        }
        return;
    }
    if (seq.p) fading = 1;
    else seq_start(track);
}

void music_enable(int on) {
    music_on = on;
    if (!on) {
        if (seq.p) fading = 1;
    } else if (!seq.p || fading) {
        if (fading && cur_track == want_track) {
            fading = 0;
            gain = 4096;
        } else if (!seq.p) {
            seq_start(want_track);
        }
    }
}

static int str_eq_ci(const char *a, const char *b, int prefix) {
    for (; *b; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return 0;
    }
    return prefix || !*a;
}

int music_track_for_region(const char *name) {
    int best = MUS_NONE, best_len = 0;
    for (const MusicRegion *r = music_regions; r->name; r++) {
        if (str_eq_ci(name, r->name, 0)) return r->track;
        int len = 0;
        while (r->name[len]) len++;
        if (len > best_len && str_eq_ci(name, r->name, 1)) {
            best = r->track;
            best_len = len;
        }
    }
    return best;
}

void music_for_region(const char *name) {
    int t = music_track_for_region(name);
    if (t != MUS_NONE) music_play(t);
}

// ------------------------------------------------------------------ PSG sound effects
typedef struct { u8 note, len; } Note;   // note = octave*12 + semitone

static const u16 x16[12] = {16032, 15131, 14283, 13482, 12725, 12011, 11336, 10699, 10099, 9533, 8997, 8493};
static u16 rate(int note) {
    int oct = note / 12, semi = note % 12;
    int sh = oct - 3;
    u32 x = sh >= 0 ? x16[semi] >> sh : x16[semi] << -sh;
    return 2048 - (x + 8) / 16;
}

#define N(o, s) ((o) * 12 + (s))
enum { C = 0, Cs, D, Ds, E, F, Fs, G, Gs, A, As, B };

static const Note s_move[] = {{N(6, E), 2}, {0, 0}};
static const Note s_ok[] = {{N(6, E), 3}, {N(6, A), 4}, {0, 0}};
static const Note s_back[] = {{N(5, A), 3}, {N(5, E), 4}, {0, 0}};
static const Note s_miss[] = {{N(3, G), 4}, {0, 0}};
static const Note s_eat[] = {{N(4, C), 4}, {N(4, G), 4}, {N(4, C), 6}, {0, 0}};
static const Note s_pickup[] = {{N(5, C), 3}, {N(5, G), 5}, {0, 0}};
static const Note s_coins[] = {{N(6, B), 3}, {N(7, E), 7}, {0, 0}};
static const Note s_err[] = {{N(3, Cs), 6}, {N(3, C), 8}, {0, 0}};
static const Note s_level[] = {{N(5, C), 6}, {N(5, E), 6}, {N(5, G), 6}, {N(6, C), 10}, {N(5, G), 5}, {N(6, C), 24}, {0, 0}};
// "Quest complete" fanfare
static const Note s_quest[] = {{N(5, G), 8}, {N(6, C), 8}, {N(6, E), 8}, {N(6, G), 14}, {0, 4}, {N(6, E), 6},
                               {N(6, G), 6}, {N(7, C), 24}, {N(6, B), 8}, {N(6, A), 8}, {N(6, B), 8}, {N(7, C), 30}, {0, 0}};
static const Note s_death[] = {{N(5, C), 10}, {N(4, G), 10}, {N(4, Ds), 10}, {N(4, C), 30}, {0, 0}};

static const Note *seq1;
static u8 seq1_i, seq1_t, seq1_duty, seq1_vol;

static void play1(const Note *s, int duty, int vol) {
    seq1 = s;
    seq1_i = 0;
    seq1_t = 0;
    seq1_duty = duty;
    seq1_vol = vol;
}

static void noise(int vol, int step, int ratio, int shift) {
    REG_SOUND4CNT_L = (vol << 12) | (step << 8);
    REG_SOUND4CNT_H = ratio | (shift << 4) | 0x8000;
}

static void duck_music(int frames) {
    duck_target = 1200;                    // ~30% while a jingle plays
    duck_timer = frames;
}

void sfx(int id) {
    switch (id) {
    case SFX_MOVE: play1(s_move, 2, 5); break;
    case SFX_OK: play1(s_ok, 2, 7); break;
    case SFX_BACK: play1(s_back, 2, 6); break;
    case SFX_CHOP: noise(10, 1, 4, 5); break;
    case SFX_MINE: noise(11, 1, 1, 2); play1(s_move, 0, 6); break;
    case SFX_SPLASH: noise(7, 3, 2, 4); break;
    case SFX_HIT: noise(12, 1, 3, 3); break;
    case SFX_MISS: play1(s_miss, 1, 6); break;
    case SFX_EAT: play1(s_eat, 1, 7); break;
    case SFX_PICKUP: play1(s_pickup, 2, 7); break;
    case SFX_FIRE: noise(6, 4, 5, 6); break;
    case SFX_LEVEL: play1(s_level, 2, 10); duck_music(90); break;
    case SFX_DEATH: play1(s_death, 1, 10); duck_music(90); break;
    case SFX_ERR: play1(s_err, 0, 7); break;
    case SFX_COINS: play1(s_coins, 2, 8); break;
    case SFX_QUEST: play1(s_quest, 2, 11); duck_music(190); break;
    }
}

static void sfx_update(void) {
    if (seq1) {
        if (seq1_t == 0) {
            const Note *n = &seq1[seq1_i];
            if (!n->len) {
                seq1 = 0;
            } else {
                if (n->note) {
                    REG_SOUND1CNT_H = (seq1_duty << 6) | (seq1_vol << 12) | (2 << 8);
                    REG_SOUND1CNT_X = rate(n->note) | 0x8000;
                }
                seq1_t = n->len;
                seq1_i++;
            }
        }
        if (seq1_t) seq1_t--;
    }
    if (duck_timer && !--duck_timer) duck_target = 4096;
}

// Called once per frame right after VBlank.
void sound_update(void) {
    int v0 = REG_VCOUNT;
    sfx_update();
    u32 pf = snd_play_frame;
    if (last_play_frame && pf - last_play_frame > 1) {
        snd_stats.frame_drops += pf - last_play_frame - 1;
        snd_stats.drop_frame = pf;
    }
    last_play_frame = pf;
    if ((s32)(mixed_frame - pf) < 1) mixed_frame = pf + 1;   // fell behind: skip ahead
    while ((s32)(mixed_frame - pf) <= 2) {
        int slot = mixed_frame & (RING_FRAMES - 1);
        mix_frame(snd_ring + slot * FRAME_SAMPLES);
        if (slot == 0) dma3_copy32(snd_ring + RING_FRAMES * FRAME_SAMPLES, snd_ring, 8);
        mixed_frame++;
        snd_stats.mixed_frames++;
    }
    snd_stats.track = cur_track;
    snd_stats.want = want_track;
    snd_stats.on = music_on;
    snd_stats.fade = fading;
    int lines = REG_VCOUNT - v0;
    if (lines < 0) lines += 228;
    snd_stats.mix_lines_last = lines;
    if (lines > snd_stats.mix_lines_max) snd_stats.mix_lines_max = lines;
}
