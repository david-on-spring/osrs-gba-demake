// Entry point and main loop.
#include "game.h"

#define BOOT_VBLANKS 3   // where boot used to end before the map moved to regions
#define BOOT_LINES 172
extern volatile u32 snd_play_frame;   // VBlanks since power-on (crt0.s, sound.c)

u8 game_state;
u32 frame;
u16 keys, keys_pressed, keys_repeat;
u16 perf_lines, perf_max;   // scanlines of work per frame (VBlank starts at 160; >= 228 = dropped frame)

static void read_keys(void) {
    static u16 prev, held;
    keys = ~REG_KEYINPUT & 0x3FF;
    keys_pressed = keys & ~prev;
    prev = keys;
    keys_repeat = keys_pressed;
    u16 dpad = keys & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT);
    if (dpad) {
        held++;
        if (held >= 16 && (held - 16) % 5 == 0) keys_repeat |= dpad;
    } else {
        held = 0;
    }
}

int main(void) {
    gfx_init();
    ui_init();
    sound_init();
    world_init();
    title_open();
    // Boot always ends at the same moment (VBlank BOOT_VBLANKS, BOOT_LINES scanlines in), so the
    // frame count that seeds the RNG on the login screen, and the mixer's catch-up after boot,
    // do not depend on how long setup took: test runs stay reproducible across code changes.
    for (;;) {
        u32 vb = snd_play_frame;
        int v = REG_VCOUNT, lines = v >= 160 ? v - 160 : v + 68;
        if (vb > BOOT_VBLANKS || (vb == BOOT_VBLANKS && lines >= BOOT_LINES)) break;
    }

    for (;;) {
        vblank_wait();
        gfx_vblank();
        sound_update();
        read_keys();
        frame++;
        if (keys_pressed && game_state == GS_TITLE) seed_rng(frame * 2654435761u ^ keys_pressed);
        oam_n = 0;
        if (game_state != GS_TITLE) pl.playtime++;

        switch (game_state) {
        case GS_TITLE: title_update(); break;
        case GS_WORLD:
            if (keys_pressed & KEY_START) {
                menu_open();
                break;
            }
            world_update();
            if (game_state == GS_WORLD) ui_hud_update();   // a quest event may have opened a dialogue
            break;
        case GS_MENU: menu_update(); break;
        case GS_DIALOG: dialog_update(); break;
        case GS_BANK: bank_update(); break;
        case GS_SHOP: shop_update(); break;
        case GS_LEVELUP: levelup_update(); break;
        case GS_QUESTDONE: questdone_update(); break;
        case GS_TARGET: target_menu_update(); break;
        }

        ui_draw_sprites();
        world_draw_sprites(0);
        obj_hide_rest();
        gfx_world_update();
        u16 v = REG_VCOUNT;
        perf_lines = v >= 160 ? v - 160 : v + 68;
        if (perf_lines > perf_max) perf_max = perf_lines;
    }
}
