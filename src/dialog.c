// Dialogue: the OSRS chatbox - parchment, the speaker's chathead (NPC on the left, the
// player on the right, none for narration), their name in dark red over centred black text,
// "Press A to continue" in blue, and "Select an Option" lists for choices. Quest scripts (quests.c) feed it pages and choices; plain townsfolk use the
// page list in their NpcDef.
#include "ui_internal.h"
static const char *const *dlg_pages;
static int dlg_page, dlg_chars, dlg_kind;
static char dlg_lines[4][48];
static int dlg_nlines, dlg_total, dlg_tx;
static s16 dlg_lx[4];                   // start x of each centred line
static u8 dlg_script, dlg_choice, dlg_sel;
static DlgEvent dlg_ev;
static s16 dlg_portrait = -1;           // NPC kind, SPK_PLAYER, or -1
static s16 dlg_px, dlg_py;

#define DLG_Y 104
// the first page clears the whole screen (HUD); later pages only the dialogue area
static u8 dlg_fresh;                    // both canvases were just cleared whole
static void dialog_clear(void) {
    if (dlg_fresh) {
        dlg_fresh = 0;
    } else if (game_state == GS_DIALOG) {
        cv_clear_rows(&cv_text, 8, 19);
        cv_clear_rows(&cv_panel, 8, 19);
    } else {
        cv_clear(&cv_text);
        cv_clear(&cv_panel);
    }
}

static s16 dlg_last_speaker = -1;      // speaker of the page on screen, -1 if none

static void dialog_layout_say(int speaker, const char *text, int prebroken) {
    cursor_hide();
    if (game_state == GS_DIALOG && !dlg_choice && speaker == dlg_last_speaker) {
        // same speaker as the page on screen: keep the box, only replace the text
        cv_fill(&cv_text, 6, DLG_Y + 13, 228, 41, 0);   // text only: keep the name
        goto text;
    }
    dialog_clear();
    icons_clear();
    dlg_choice = 0;
    dlg_last_speaker = speaker;
    draw_parchment(4, DLG_Y, 232, 55);
    const char *name = 0;
    dlg_portrait = -1;
    dlg_tx = 120;                         // centre of the text area
    if (speaker == SPK_PLAYER) {
        name = player_name;
        dlg_portrait = SPK_PLAYER;
        dlg_px = 198;
        dlg_py = DLG_Y + 10;
        dlg_tx = 104;
    } else if (speaker != SPK_NONE) {
        name = npc_defs[speaker].name;
        dlg_portrait = speaker;
        dlg_px = 10;
        dlg_py = DLG_Y + 10;
        dlg_tx = 138;
    }
    if (name) text_center_ns(dlg_tx, DLG_Y + 3, name, TC_DKRED);
text:
    if (prebroken) {
        // quest pages are wrapped at build time, lines separated by '\n'
        dlg_nlines = 0;
        while (*text && dlg_nlines < 3) {
            int k = 0;
            while (*text && *text != '\n' && k < 47) dlg_lines[dlg_nlines][k++] = *text++;
            dlg_lines[dlg_nlines++][k] = 0;
            if (*text == '\n') text++;
        }
    } else {
        dlg_nlines = text_wrap(text, 172, dlg_lines, 3);
    }
    dlg_total = 0;
    for (int i = 0; i < dlg_nlines; i++) {
        const char *p = dlg_lines[i];
        while (*p++) dlg_total++;
        dlg_lx[i] = dlg_tx - text_width(dlg_lines[i]) / 2;
    }
    dlg_chars = 0;
    show_arrow = 0;
}

static void dialog_choice_row(int i, int y) {
    int yy = y + 17 + i * 12;
    if (i == dlg_sel) cursor_to(4, yy + 1);
    text_center_ns(120, yy, dlg_ev.opt[i], i == dlg_sel ? TC_WHITE : TC_BLACK);
}

static int dialog_choice_y(void) { return 158 - (dlg_ev.nopt * 12 + 20); }

static void dialog_layout_choice(void) {
    dialog_clear();
    icons_clear();
    dlg_choice = 1;
    dlg_last_speaker = -1;
    dlg_portrait = -1;
    show_arrow = 0;
    int n = dlg_ev.nopt;
    int y = dialog_choice_y();
    draw_parchment(4, y, 232, n * 12 + 20);
    text_center_ns(120, y + 4, "Select an Option", TC_DKRED);
    dlg_choice = 2;   // the option rows follow next frame (keeps each frame's work small)
}

// moving the selection only redraws the two rows involved
static void dialog_choice_move(int old) {
    int y = dialog_choice_y();
    for (int k = 0; k < 2; k++) {
        int i = k ? dlg_sel : old, yy = y + 17 + i * 12;
        cv_fill(&cv_text, 6, yy, 228, 11, 0);
        dialog_choice_row(i, y);
    }
}

static void dialog_enter(void) {
    if (game_state == GS_DIALOG) return;
    game_state = GS_DIALOG;
    player_stop();
    gfx_set_ui_alpha(13, 0);
    sfx(SFX_OK);
}

static void dialog_close(void) {
    dlg_last_speaker = -1;
    show_arrow = 0;
    dlg_portrait = -1;
    dlg_choice = 0;
    cursor_hide();
    icons_clear();
    game_state = GS_WORLD;
    gfx_set_ui_alpha(12, 0);
    ui_hud_reset();
}

// next page/choice of the running quest script; closes the dialogue when it ends

static void dialog_vm_next(void) {
    if (qvm_step(&dlg_ev)) {
        int first = game_state != GS_DIALOG;
        if (first) {
            dlg_last_speaker = -1;
            cv_clear(&cv_text);
            cv_clear(&cv_panel);
            dlg_fresh = 1;
        }
        dialog_enter();
        if (dlg_ev.type == DLG_SAY) {
            dialog_layout_say(dlg_ev.speaker, dlg_ev.text, 1);
        } else {
            dlg_sel = 0;
            dialog_layout_choice();
        }
        return;
    }
    dlg_script = 0;
    int after = qvm_after();
    if (after == 1) shop_open();
    else if (after == 2) bank_open();
    else if (game_state == GS_DIALOG) dialog_close();
}

void dialog_run_script(void) {
    dlg_script = 1;
    dialog_vm_next();
}

void dialog_open(int kind, int index) {
    (void)index;
    dlg_script = 0;
    dlg_kind = kind;
    dlg_pages = npc_defs[kind].dialog;
    dlg_page = 0;
    if (!dlg_pages) return;
    dlg_last_speaker = -1;
    cv_clear(&cv_text);
    cv_clear(&cv_panel);
    dlg_fresh = 1;
    dialog_enter();
    dialog_layout_say(kind, dlg_pages[0], 0);
}

static void dialog_draw_chars(int from, int to) {
    int k = 0;
    for (int l = 0; l < dlg_nlines; l++) {
        int x = dlg_lx[l];
        for (const char *p = dlg_lines[l]; *p; p++, k++) {
            char c[2] = {*p, 0};
            if (k >= from && k < to) text_draw_ns(x, DLG_Y + 14 + l * 10, c, TC_BLACK);
            x += font_width[(u8)*p - 32] + 1;
        }
    }
    if (to >= dlg_total) text_center_ns(dlg_tx, DLG_Y + 45, "Press A to continue", TC_BLUE);   // below line 3 (ends +43)
}

void dialog_update(void) {
    if (dlg_choice == 2) {
        for (int i = 0; i < dlg_ev.nopt; i++) dialog_choice_row(i, dialog_choice_y());
        dlg_choice = 1;
        return;
    }
    if (dlg_choice) {
        int n = dlg_ev.nopt;
        if (keys_repeat & (KEY_UP | KEY_DOWN)) {
            int old = dlg_sel;
            dlg_sel = (dlg_sel + ((keys_repeat & KEY_UP) ? n - 1 : 1)) % n;
            sfx(SFX_MOVE);
            dialog_choice_move(old);
        }
        if (keys_pressed & KEY_A) {
            sfx(SFX_OK);
            cursor_hide();
            qvm_choose(dlg_sel);
            dialog_vm_next();
        }
        return;
    }
    if (dlg_chars < dlg_total) {
        int step = (keys & KEY_A) ? 4 : 1;
        int to = dlg_chars + step;
        if (to > dlg_total) to = dlg_total;
        dialog_draw_chars(dlg_chars, to);
        if ((frame & 3) == 0) sfx(SFX_MOVE);
        dlg_chars = to;
        if (keys_pressed & KEY_B) {
            dialog_draw_chars(dlg_chars, dlg_total);
            dlg_chars = dlg_total;
        }
        return;
    }
    show_arrow = 1;
    if (keys_pressed & (KEY_A | KEY_B)) {
        sfx(SFX_OK);
        if (dlg_script) {
            dialog_vm_next();
            return;
        }
        if (dlg_pages[dlg_page + 1] && !(keys_pressed & KEY_B)) {
            dlg_page++;
            dialog_layout_say(dlg_kind, dlg_pages[dlg_page], 0);
            return;
        }
        const NpcDef *d = &npc_defs[dlg_kind];
        if (d->flags & NF_BANK) {
            show_arrow = 0;
            dlg_portrait = -1;
            bank_open();
        } else if (d->flags & NF_SHOP) {
            show_arrow = 0;
            dlg_portrait = -1;
            shop_open();
        } else {
            dialog_close();
        }
    }
}

// ui_wait while a dialogue is up: 1 page fully shown, 2 choice box, 0 otherwise
int dialog_wait_state(void) { return dlg_choice == 1 ? 2 : dlg_choice ? 0 : dlg_chars >= dlg_total ? 1 : 0; }

void draw_dialog_portrait(void) {
    if (game_state != GS_DIALOG || dlg_portrait < 0) return;
    if (dlg_portrait == SPK_PLAYER) {
        avatar_draw_portrait(dlg_px, dlg_py, 1);
        return;
    }
    const NpcDef *d = &npc_defs[dlg_portrait];
    int t = npc_portrait_tile(dlg_portrait);
    if (d->sprite == SPR_KIND_DEMON) obj_sprite(dlg_px, dlg_py, t, d->pal, 1, 0, 0, OBJF_32);
    else obj_sprite(dlg_px, dlg_py, t, d->pal, 1, 0, 1, OBJF_AFFINE2X | (d->sprite == SPR_KIND_GHOST ? OBJF_SEMI : 0));
}
