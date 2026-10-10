// Headless test driver: runs the ROM in libmgba, feeds scripted input, writes PNG screenshots.
// usage: harness rom.gba script.txt [save.sav]
//   script lines: wait N | hold KEYS N | press KEYS | shot file.png   (KEYS like A, START, UP+B)
//                 audio on | audio off | wav file.wav   (capture the mixed audio output to a WAV)
//                 until8 ADDR VAL [MAXFRAMES] | expect8/expect16 ADDR VAL LABEL   (test helpers)
#include <mgba/core/core.h>
#include <mgba/core/blip_buf.h>
#include <mgba/core/interface.h>
#include <mgba/core/log.h>
#include <stdarg.h>
#include <mgba/gba/core.h>
#include <mgba-util/vfs.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static color_t fb[240 * 160];
static void nolog(struct mLogger *l, int c, enum mLogLevel lv, const char *f, va_list a) {
    (void)l; (void)c; (void)f; (void)a;
    if (lv & (mLOG_FATAL | mLOG_ERROR)) { vfprintf(stderr, f, a); fputc(10, stderr); }
}
static struct mLogger quiet = {.log = nolog};

static unsigned crc_table[256];
static void crc_init(void) {
    for (unsigned n = 0; n < 256; n++) {
        unsigned c = n;
        for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
}
static unsigned crc(unsigned c, const unsigned char *b, size_t n) {
    c ^= 0xFFFFFFFFu;
    while (n--) c = crc_table[(c ^ *b++) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}
static void be32(unsigned char *p, unsigned v) {
    p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}
static void chunk(FILE *f, const char *type, const unsigned char *data, unsigned len) {
    unsigned char h[8];
    be32(h, len);
    memcpy(h + 4, type, 4);
    fwrite(h, 1, 8, f);
    fwrite(data, 1, len, f);
    unsigned c = crc(0, (const unsigned char *)type, 4);
    c = crc(c, data, len);
    be32(h, c);
    fwrite(h, 1, 4, f);
}

static void write_png(const char *path, int scale) {
    int w = 240 * scale, h = 160 * scale;
    size_t rowlen = 1 + w * 3;
    size_t rawlen = rowlen * h;
    unsigned char *raw = malloc(rawlen);
    for (int y = 0; y < h; y++) {
        unsigned char *r = raw + y * rowlen;
        *r++ = 0;
        for (int x = 0; x < w; x++) {
            color_t c = fb[(y / scale) * 240 + x / scale];
            *r++ = c & 0xFF;
            *r++ = (c >> 8) & 0xFF;
            *r++ = (c >> 16) & 0xFF;
        }
    }
    size_t nblocks = (rawlen + 65534) / 65535;
    size_t zlen = 2 + rawlen + nblocks * 5 + 4;
    unsigned char *z = malloc(zlen), *p = z;
    *p++ = 0x78; *p++ = 0x01;
    unsigned a = 1, b = 0;
    for (size_t i = 0; i < rawlen; i++) { a = (a + raw[i]) % 65521; b = (b + a) % 65521; }
    for (size_t off = 0; off < rawlen; off += 65535) {
        size_t n = rawlen - off > 65535 ? 65535 : rawlen - off;
        *p++ = off + n >= rawlen;
        *p++ = n & 0xFF; *p++ = n >> 8; *p++ = ~n & 0xFF; *p++ = (~n >> 8) & 0xFF;
        memcpy(p, raw + off, n);
        p += n;
    }
    be32(p, (b << 16) | a);
    p += 4;
    FILE *f = fopen(path, "wb");
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    unsigned char ihdr[13];
    be32(ihdr, w); be32(ihdr + 4, h);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = ihdr[11] = ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13);
    chunk(f, "IDAT", z, p - z);
    chunk(f, "IEND", 0, 0);
    fclose(f);
    free(raw);
    free(z);
}

// ---- audio capture: 16-bit stereo at 32768 Hz, drained after every frame ----
#define AUDIO_RATE 32768
static struct mCore *gcore;
static short *abuf;
static size_t alen, acap;
static int arec;
static void run_frame(void) {
    gcore->runFrame(gcore);
    blip_t *l = gcore->getAudioChannel(gcore, 0), *r = gcore->getAudioChannel(gcore, 1);
    int avail = blip_samples_avail(l);
    if (!arec) {
        if (avail > 0) {
            short tmp[4096];
            while (avail > 0) {
                int n = avail > 2048 ? 2048 : avail;
                blip_read_samples(l, tmp, n, 1);
                blip_read_samples(r, tmp + 1, n, 1);
                avail -= n;
            }
        }
        return;
    }
    if (alen + 2 * (size_t)avail > acap) {
        acap = (alen + 2 * avail) * 2 + 65536;
        abuf = realloc(abuf, acap * sizeof(short));
    }
    blip_read_samples(l, abuf + alen, avail, 1);
    blip_read_samples(r, abuf + alen + 1, avail, 1);
    alen += 2 * avail;
}
static void le32(unsigned char *p, unsigned v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static void write_wav(const char *path) {
    unsigned char h[44];
    unsigned bytes = alen * 2;
    memcpy(h, "RIFF", 4); le32(h + 4, 36 + bytes); memcpy(h + 8, "WAVEfmt ", 8);
    le32(h + 16, 16); h[20] = 1; h[21] = 0; h[22] = 2; h[23] = 0;
    le32(h + 24, AUDIO_RATE); le32(h + 28, AUDIO_RATE * 4); h[32] = 4; h[33] = 0; h[34] = 16; h[35] = 0;
    memcpy(h + 36, "data", 4); le32(h + 40, bytes);
    FILE *f = fopen(path, "wb");
    fwrite(h, 1, 44, f);
    fwrite(abuf, 2, alen, f);
    fclose(f);
}

static unsigned parse_keys(const char *s) {
    static const char *names[] = {"A", "B", "SELECT", "START", "RIGHT", "LEFT", "UP", "DOWN", "R", "L"};
    unsigned k = 0;
    char buf[64];
    strncpy(buf, s, 63);
    buf[63] = 0;
    for (char *t = strtok(buf, "+"); t; t = strtok(0, "+"))
        for (int i = 0; i < 10; i++)
            if (!strcmp(t, names[i])) k |= 1u << i;
    return k;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s rom.gba script.txt [save.sav]\n", argv[0]);
        return 1;
    }
    crc_init();
    mLogSetDefaultLogger(&quiet);
    struct mCore *core = GBACoreCreate();
    core->init(core);
    mCoreInitConfig(core, 0);
    core->setVideoBuffer(core, fb, 240);
    if (!mCoreLoadFile(core, argv[1])) {
        fprintf(stderr, "cannot load %s\n", argv[1]);
        return 1;
    }
    if (argc > 3) {
        struct VFile *sv = VFileOpen(argv[3], O_CREAT | O_RDWR);
        if (sv) core->loadSave(core, sv);
    }
    core->reset(core);
    gcore = core;
    core->setAudioBufferSize(core, 4096);
    blip_set_rates(core->getAudioChannel(core, 0), core->frequency(core), AUDIO_RATE);
    blip_set_rates(core->getAudioChannel(core, 1), core->frequency(core), AUDIO_RATE);
    FILE *s = fopen(argv[2], "r");
    char line[256];
    long total = 0;
    while (fgets(line, sizeof line, s)) {
        char cmd[32] = {0}, arg[200] = {0};
        int n = 0;
        if (line[0] == '#' || sscanf(line, "%31s", cmd) < 1) continue;
        if (!strcmp(cmd, "wait")) {
            sscanf(line, "%*s %d", &n);
            core->setKeys(core, 0);
            for (int i = 0; i < n; i++) run_frame();
            total += n;
        } else if (!strcmp(cmd, "hold")) {
            sscanf(line, "%*s %199s %d", arg, &n);
            core->setKeys(core, parse_keys(arg));
            for (int i = 0; i < n; i++) run_frame();
            core->setKeys(core, 0);
            total += n;
        } else if (!strcmp(cmd, "press")) {
            sscanf(line, "%*s %199s", arg);
            core->setKeys(core, parse_keys(arg));
            run_frame();
            run_frame();
            core->setKeys(core, 0);
            for (int i = 0; i < 4; i++) run_frame();
            total += 6;
        } else if (!strcmp(cmd, "line")) {
            sscanf(line, "%*s %d", &n);
            int guard = 0;
            while (core->busRead16(core, 0x04000006) != (unsigned)n && guard++ < 2000000) core->step(core);
        } else if (!strcmp(cmd, "poke16") || !strcmp(cmd, "poke8")) {
            unsigned addr = 0, val = 0;
            sscanf(line, "%*s %x %x", &addr, &val);
            if (cmd[4] == '1') core->busWrite16(core, addr, val);
            else core->busWrite8(core, addr, val);
        } else if (!strcmp(cmd, "until8")) {
            // run (keys released) until the byte at addr equals val, at most n frames
            unsigned addr = 0, val = 0;
            n = 600;
            sscanf(line, "%*s %x %x %d", &addr, &val, &n);
            core->setKeys(core, 0);
            int f = 0;
            while (f < n && core->busRead8(core, addr) != val) {
                run_frame();
                f++;
            }
            total += f;
            if (f >= n) printf("until8 %08x == %x timed out after %d frames (frame %ld)\n", addr, val, n, total);
        } else if (!strcmp(cmd, "dlgrun")) {
            // continue dialogue pages (byte at addr == 1: press A) until it shows something
            // else that needs a decision: 2 choice, 3/4 popup, 5 back in the world
            unsigned addr = 0;
            n = 3000;
            sscanf(line, "%*s %x %d", &addr, &n);
            int f = 0;
            for (;;) {
                unsigned st = core->busRead8(core, addr);
                if (st >= 2 || f >= n) break;
                core->setKeys(core, st == 1 ? 1 : 0);
                run_frame();
                f++;
                if (st == 1) {   // like 'press A': hold two frames, release four
                    run_frame();
                    core->setKeys(core, 0);
                    for (int k = 0; k < 4; k++) run_frame();
                    f += 5;
                }
            }
            core->setKeys(core, 0);
            total += f;
            if (f >= n) printf("dlgrun timed out (frame %ld)\n", total);
        } else if (!strcmp(cmd, "expectinv")) {
            // expectinv ADDR ID LABEL: some inventory slot (28 x {u16 id, u16 qty} at ADDR) holds ID
            unsigned addr = 0, val = 0, found = 0;
            sscanf(line, "%*s %x %x %199s", &addr, &val, arg);
            for (int i = 0; i < 28 && !found; i++) found = core->busRead16(core, addr + i * 4) == val;
            printf("%s %s\n", found ? "PASS" : "FAIL", arg);
        } else if (!strcmp(cmd, "expect8") || !strcmp(cmd, "expect16")) {
            unsigned addr = 0, val = 0;
            sscanf(line, "%*s %x %x %199s", &addr, &val, arg);
            unsigned got = cmd[6] == '8' ? core->busRead8(core, addr) : core->busRead16(core, addr);
            printf("%s %s (got %x, want %x)\n", got == val ? "PASS" : "FAIL", arg, got, val);
        } else if (!strcmp(cmd, "dump")) {
            unsigned addr = 0;
            sscanf(line, "%*s %x %d", &addr, &n);
            printf("%08x:", addr);
            for (int i = 0; i < n; i++) printf(" %04x", core->busRead16(core, addr + i * 2));
            printf("\n");
        } else if (!strcmp(cmd, "audio")) {
            sscanf(line, "%*s %199s", arg);
            arec = !strcmp(arg, "on");
            if (arec) alen = 0;
        } else if (!strcmp(cmd, "wav")) {
            sscanf(line, "%*s %199s", arg);
            write_wav(arg);
            printf("wav %s: %zu samples at frame %ld\n", arg, alen / 2, total);
        } else if (!strcmp(cmd, "shot")) {
            sscanf(line, "%*s %199s", arg);
            write_png(arg, 2);
            printf("shot %s at frame %ld\n", arg, total);
        }
    }
    core->deinit(core);
    return 0;
}
