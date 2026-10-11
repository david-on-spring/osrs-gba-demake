// Entry point and main loop.
#include "game.h"
#include "map.h"

extern volatile u32 snd_play_frame;   // VBlanks since power-on (crt0.s, sound.c)

u8 game_state;
u32 frame;
u16 keys, keys_pressed, keys_repeat;
u16 perf_lines, perf_max;   // scanlines of work per frame from VBlank (>= 228: a VBlank was missed)

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

    for (;;) {
        vblank_wait();
        u32 vb0 = snd_play_frame;
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
        case GS_SMITH: smithy_update(); break;
        }

        ui_draw_sprites();
        world_draw_sprites(0);
        obj_hide_rest();
        gfx_world_update();
        if (game_state != GS_TITLE) {   // spare scanlines: decompress the map around the camera
            int x, y, p;
            world_focus(&x, &y, &p);
            map_prefetch(x, y, p, vb0);
        }
        u16 v = REG_VCOUNT;
        perf_lines = (snd_play_frame - vb0) * 228 + (v >= 160 ? v - 160 : v + 68);
        if (perf_lines > perf_max) perf_max = perf_lines;
    }
}
