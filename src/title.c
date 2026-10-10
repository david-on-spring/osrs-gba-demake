// Title screen: the OSRS login screen - black, the gold logo between columns of flame, a
// stone "Welcome to RuneScape" box with New User / Existing User, and the music toggle
// bottom-right. New User asks for a display name on an on-screen keyboard, then a save slot;
// Existing User picks one of the three saved characters.
#include "ui_internal.h"
enum { TS_NEW, TS_EXISTING, TS_MUSIC };
enum { TM_MAIN, TM_NAME, TM_SLOTS };
static u8 title_sel, title_phase, title_mute, title_back, title_mode;
static s16 title_t;
static char name_buf[NAME_MAX + 1];
static u8 name_len, kb_r, kb_c, slot_sel, slot_confirm;
static const char *title_note;

#define TB_Y 91   // login buttons
static const u8 note_px[11] = {0x7C, 0x7C, 0x44, 0x44, 0x44, 0x44, 0x44, 0x66, 0xEE, 0xEE, 0x44};   // beamed quavers

// display names: letters, digits, spaces, hyphens and underscores, up to 12 (as in OSRS)
#define KB_COLS 13
#define KB_ROWS 5
static const char kb_keys[KB_ROWS][KB_COLS + 1] = {
    "ABCDEFGHIJKLM", "NOPQRSTUVWXYZ", "abcdefghijklm", "nopqrstuvwxyz", "0123456789 -_",
};

static void title_music_button(void) {
    int x = 214, y = 134;
    draw_stone_button(x, y, 22, 22, title_mode == TM_MAIN && title_sel == TS_MUSIC);
    for (int r = 0; r < 11; r++)
        for (int b = 0; b < 8; b++)
            if (note_px[r] & (0x80 >> b)) cv_pset(&cv_panel, x + 7 + b, y + 5 + r, PC_GOLD);
    if (title_mute)
        for (int i = 0; i < 14; i++) {
            cv_pset(&cv_panel, x + 4 + i, y + 4 + i, PC_RED);
            cv_pset(&cv_panel, x + 5 + i, y + 4 + i, PC_RED);
        }
}

static void title_draw_name(void) {
    draw_panel(42, 58, 156, 100, 0);
    text_center(120, 62, "Choose a display name", TC_YELLOW);
    draw_cell(60, 74, 120, 14, 0);
    char shown[NAME_MAX + 2];
    int k = 0;
    for (; k < name_len; k++) shown[k] = name_buf[k];
    if (name_len < NAME_MAX) shown[k++] = '_';
    shown[k] = 0;
    text_center(120, 76, shown, TC_WHITE);
    for (int r = 0; r < KB_ROWS; r++)
        for (int c = 0; c < KB_COLS; c++) {
            int x = 49 + c * 11, y = 92 + r * 11;
            if (r == kb_r && c == kb_c) draw_stone_button(x, y, 10, 10, 1);
            char ch[2] = {kb_keys[r][c], 0};
            if (ch[0] == ' ') ch[0] = '.';   // the space key
            text_draw(x + 5 - text_width(ch) / 2, y + 1, ch, r == kb_r && c == kb_c ? TC_YELLOW : TC_WHITE);
        }
    text_center(120, 147, title_note ? title_note : "A type  B delete  START done", title_note ? TC_RED : TC_DIM);
}

static void title_draw_slots(void) {
    draw_panel(30, 58, 180, 92, 0);
    text_center(120, 62, title_back == TS_NEW ? "Choose a save slot" : "Choose your character", TC_YELLOW);
    for (int k = 0; k < SAVE_SLOTS; k++) {
        int y = 76 + k * 22;
        char nm[NAME_MAX + 1];
        int total;
        u32 t;
        int used = save_slot_summary(k, nm, &total, &t);
        int dim = !used && title_back == TS_EXISTING;
        draw_stone_button(38, y, 164, 19, k == slot_sel);
        char *p = sadd_num(sbuf, k + 1);
        sadd(p, ".");
        text_draw(44, y + 5, sbuf, TC_DIM);
        if (used) {
            text_draw(56, y + 1, nm, TC_WHITE);
            p = sadd(sbuf, "Total level ");
            sadd_num(p, total);
            text_draw(56, y + 10, sbuf, TC_ORANGE);
            (void)t;   // time played is Hans's to tell (Lumbridge Castle), as in OSRS
        } else {
            text_draw(56, y + 5, "Empty slot", dim ? TC_DIM : TC_GREY);
        }
    }
    const char *hint = slot_confirm ? "Press A again to overwrite" : title_note ? title_note : "A select  B back";
    text_center(120, 141, hint, slot_confirm || title_note ? TC_RED : TC_DIM);
}

static void title_draw(void) {
    cv_clear(&cv_text);
    dma3_copy32(cv_panel.tiles, title_bg, 600 * 8);   // the stone hall (tools/gen_title.py)
    cv_panel.dmin = 0;
    cv_panel.dmax = 19;
    icons_clear();
    title_music_button();
    if (title_mode == TM_NAME) {
        title_draw_name();
        return;
    }
    if (title_mode == TM_SLOTS) {
        title_draw_slots();
        return;
    }
    text_center(120, 54, "Game Boy Advance Edition", TC_WHITE);
    // welcome box
    draw_panel(46, 66, 148, 52, 0);
    text_center(120, 74, "Welcome to RuneScape", TC_YELLOW);
    int has = save_exists();
    static const char *const opts[2] = {"New User", "Existing User"};
    for (int i = 0; i < 2; i++) {
        int x = i ? 122 : 54, w = i ? 64 : 60;
        draw_stone_button(x, TB_Y, w, 20, title_sel == i);
        text_center(x + w / 2, TB_Y + 6, opts[i], (i == TS_EXISTING && !has) ? TC_DIM : TC_WHITE);
    }
    text_center(120, 124, "A fan-made tribute. Not affiliated with Jagex.", TC_WHITE);
}

void title_open(void) {
    game_state = GS_TITLE;
    gfx_set_title_palette(1);
    hud_clear_state();   // a fresh chatbox after logging out
    music_play(MUS_TITLE);
    save_migrate();
    title_mode = TM_MAIN;
    title_sel = save_exists() ? TS_EXISTING : TS_NEW;
    title_back = title_sel;
    title_phase = 0;
    title_t = 0;
    title_note = 0;
    cursor_hide();
    gfx_set_world_fade(0);
    gfx_set_mosaic(0);
    gfx_set_ui_alpha(16, 0);
    title_draw();
}

// the flames either side of the logo (OBJ fire frames at 2x), behind the login boxes
void title_flames(void) {
    if (game_state != GS_TITLE || title_phase) return;
    // flames rising from the braziers' bowls (tools/gen_title.py draws them at x 22 and 218)
    for (int side = 0; side < 2; side++)
        for (int k = 0; k < 2; k++) {
            int x = (side ? 218 : 22) - 16 + (k ? 7 : -7), y = 97 - 64 + (k ? 6 : 0);
            int f = ((frame / 6) + k * 2 + side) & 3;
            obj_sprite(x, y, SPR_TFLAME0 + f * 8, 6, 1, k, 1, OBJF_AFFINE2X | OBJF_TALL);
        }
}

static void title_start(int is_new) {
    sfx(SFX_LEVEL);
    world_init();
    save_set_slot(slot_sel);
    if (!is_new && load_game()) {
        world_after_load();
    } else {
        world_new_game();
        for (int i = 0; i <= NAME_MAX; i++) player_name[i] = name_buf[i];
        if (title_mute) pl.music = 0;
        save_game();   // the new character claims its slot right away
    }
    if (title_mute) pl.music = 0;   // the login screen's music switch carries into the game
    cursor_hide();
    icons_clear();
    cv_clear(&cv_text);
    cv_clear(&cv_panel);
    title_phase = 1;
    // snap camera to the player for the fade in (without treating the A press as an
    // interaction with whatever the loaded player faces)
    keys_pressed = 0;
    world_update();
}

static void title_name_input(void) {
    int r = kb_r, c = kb_c;
    if (keys_repeat & KEY_LEFT) c = (c + KB_COLS - 1) % KB_COLS;
    if (keys_repeat & KEY_RIGHT) c = (c + 1) % KB_COLS;
    if (keys_repeat & KEY_UP) r = (r + KB_ROWS - 1) % KB_ROWS;
    if (keys_repeat & KEY_DOWN) r = (r + 1) % KB_ROWS;
    int dirty = 0;
    if (r != kb_r || c != kb_c) {
        kb_r = r, kb_c = c;
        sfx(SFX_MOVE);
        dirty = 1;
    }
    if (keys_pressed & KEY_A) {
        char ch = kb_keys[kb_r][kb_c];
        // no leading space, no double spaces (OSRS name rules)
        if (name_len < NAME_MAX && !(ch == ' ' && (!name_len || name_buf[name_len - 1] == ' '))) {
            name_buf[name_len++] = ch;
            name_buf[name_len] = 0;
            sfx(SFX_OK);
        } else {
            sfx(SFX_ERR);
        }
        title_note = 0;
        dirty = 1;
    }
    if (keys_pressed & KEY_B) {
        if (name_len) {
            name_buf[--name_len] = 0;
            sfx(SFX_BACK);
        } else {
            title_mode = TM_MAIN;   // back out of an empty name
            sfx(SFX_BACK);
        }
        title_note = 0;
        dirty = 1;
    }
    if (keys_pressed & KEY_START) {
        while (name_len && name_buf[name_len - 1] == ' ') name_buf[--name_len] = 0;
        if (!name_len) {
            title_note = "Please enter a name.";
            sfx(SFX_ERR);
        } else {
            title_mode = TM_SLOTS;
            slot_confirm = 0;
            slot_sel = 0;
            for (int k = SAVE_SLOTS - 1; k >= 0; k--)
                if (!save_slot_used(k)) slot_sel = k;   // first empty slot
            title_note = 0;
            sfx(SFX_OK);
        }
        dirty = 1;
    }
    if (dirty) title_draw();
}

static void title_slots_input(void) {
    int s = slot_sel, dirty = 0;
    if (keys_repeat & KEY_UP) s = (s + SAVE_SLOTS - 1) % SAVE_SLOTS;
    if (keys_repeat & KEY_DOWN) s = (s + 1) % SAVE_SLOTS;
    if (s != slot_sel) {
        slot_sel = s;
        slot_confirm = 0;
        title_note = 0;
        sfx(SFX_MOVE);
        dirty = 1;
    }
    if (keys_pressed & (KEY_A | KEY_START)) {
        int used = save_slot_used(slot_sel);
        if (title_back == TS_EXISTING) {
            if (used) {
                title_start(0);
                return;
            }
            title_note = "That slot is empty.";
            sfx(SFX_ERR);
        } else if (used && !slot_confirm) {
            slot_confirm = 1;
            sfx(SFX_ERR);
        } else {
            title_start(1);
            return;
        }
        dirty = 1;
    }
    if (keys_pressed & KEY_B) {
        title_mode = title_back == TS_NEW ? TM_NAME : TM_MAIN;
        slot_confirm = 0;
        title_note = 0;
        sfx(SFX_BACK);
        dirty = 1;
    }
    if (dirty) title_draw();
}

void title_update(void) {
    title_t++;
    if (title_phase) {
        if (++title_phase > 12) {
            gfx_set_title_palette(0);
            game_state = GS_WORLD;
            gfx_set_world_fade(16);
            gfx_set_ui_alpha(12, 0);
            ui_hud_reset();
            msg_col("Welcome to RuneScape.", TC_GOLD);
            if (title_back == TS_NEW) msg("Talk to the Lumbridge Guide east of the castle for tips.");
            music_enable(pl.music);
        } else {
            gfx_set_world_fade(title_phase * 16 / 12);
            gfx_set_ui_alpha(16 - title_phase * 16 / 12, 1);
        }
        return;
    }
    if (keys_pressed & KEY_SELECT) {
        title_mute ^= 1;
        music_enable(!title_mute);
        sfx(SFX_OK);
        title_music_button();
        return;
    }
    if (title_mode == TM_NAME) {
        title_name_input();
        return;
    }
    if (title_mode == TM_SLOTS) {
        title_slots_input();
        return;
    }
    int has = save_exists();
    int s = title_sel;
    if (s == TS_MUSIC) {
        if (keys_repeat & (KEY_UP | KEY_LEFT)) s = title_back;
    } else {
        if (keys_repeat & (KEY_LEFT | KEY_RIGHT)) s ^= 1;
        if (s == TS_EXISTING && !has) s = TS_NEW;
        if (keys_repeat & KEY_DOWN) {
            title_back = s;
            s = TS_MUSIC;
        }
    }
    if (s != title_sel) {
        title_sel = s;
        sfx(SFX_MOVE);
        title_draw();
    }
    if (title_sel == TS_MUSIC && (keys_pressed & (KEY_A | KEY_START))) {
        title_mute ^= 1;
        music_enable(!title_mute);
        sfx(SFX_OK);
        title_music_button();
        return;
    }
    if (keys_pressed & (KEY_A | KEY_START)) {
        title_back = title_sel;
        title_note = 0;
        if (title_sel == TS_NEW) {
            title_mode = TM_NAME;
            name_len = 0;
            name_buf[0] = 0;
            kb_r = kb_c = 0;
        } else {
            title_mode = TM_SLOTS;
            slot_confirm = 0;
            slot_sel = 0;
            for (int k = SAVE_SLOTS - 1; k >= 0; k--)
                if (save_slot_used(k)) slot_sel = k;   // first used slot
        }
        sfx(SFX_OK);
        title_draw();
    }
}
