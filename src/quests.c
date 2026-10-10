// Quest engine: progress state lives in the Player struct (qstage/qvar, saved with it); all
// quest content is data compiled from tools/quests.txt by tools/gen_quests.py:
//   quest_defs[]   names, quest points, journal text per stage, item checklists, rewards
//   quest_code[]   dialogue/interaction scripts as a small bytecode, run by the VM below
//   entry tables   which script runs when talking to an NPC kind, using a map object, using
//                  an item from the menu, or on an event (Delrith's defeat)
//   quest_show[]   NPCs that only exist during part of a quest (ghost, skeleton, Delrith)
// The VM runs until it has something to show (a dialogue page or a choice), hands that to the
// dialogue box in ui.c, and resumes when the player continues.
#include "game.h"

#define VM_IDLE 0xFFFF
enum { AFTER_NONE, AFTER_SHOP, AFTER_BANK };

static struct {
    u16 pc;
    s16 npc;               // npcs[] index the script is about, -1 for objects/items
    u8 kind;               // NPC kind for the "npc" speaker, 0xFF none
    u8 after;              // open the shop/bank when the script ends
    u8 acted;              // the script showed or changed something
    u16 targets[5];        // jump targets of the choice on screen
} vm = {VM_IDLE, -1, 0xFF, 0, 0, {0}};

static s8 pending[QUEST_COUNT];
static u8 npending;

static inline u16 rd16(int at) { return quest_code[at] | (quest_code[at + 1] << 8); }

int quest_stage(int q) { return pl.qstage[q]; }
int quest_done(int q) { return pl.qstage[q] >= quest_defs[q].done; }

int quest_points(void) {
    int n = 0;
    for (int q = 0; q < QUEST_COUNT; q++)
        if (quest_done(q)) n += quest_defs[q].qp;
    return n;
}

void quest_new_game(void) {
    for (int q = 0; q < 16; q++) pl.qstage[q] = 0, pl.qvar[q] = 0;
    npending = 0;
    vm.pc = VM_IDLE;
}

int quest_pending(void) {
    if (!npending) return -1;
    int q = pending[0];
    for (int i = 1; i < npending; i++) pending[i - 1] = pending[i];
    npending--;
    return q;
}

static int cheb(int ax, int ay, int bx, int by) {
    int dx = ax - bx, dy = ay - by;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    return dx > dy ? dx : dy;
}

static int cond(int c, int a, int b) {
    switch (c) {
    case C_STAGE_EQ: return pl.qstage[a] == b;
    case C_STAGE_NE: return pl.qstage[a] != b;
    case C_STAGE_GE: return pl.qstage[a] >= b;
    case C_STAGE_LE: return pl.qstage[a] <= b;
    case C_STAGE_GT: return pl.qstage[a] > b;
    case C_STAGE_LT: return pl.qstage[a] < b;
    case C_HAS: return b <= 1 ? has_item(a) : inv_count(a) >= b;
    case C_WORN:
        for (int i = 0; i < EQ_COUNT; i++)
            if (pl.equip[i] == a) return 1;
        return 0;
    case C_FLAG: return (pl.qvar[a] >> b) & 1;
    case C_VAR_GE: return pl.qvar[a] >= b;
    case C_NEAR: return cheb(pl.x, pl.y, a & 0xFF, a >> 8) <= b;
    case C_QP_GE: return quest_points() >= b;
    case C_FREE: return inv_free() >= b;
    case C_CHANCE: return rnd_range(100) < b;
    case C_DONE: return quest_done(a);
    case C_FACING: return pl.dir == b;
    case C_LEVEL_GE: return skill_level(a) >= b;
    case C_NPCSTATE: return vm.npc >= 0 && npcs[vm.npc].state;
    case C_CLEAR: return player_can_walk_through();
    }
    return 0;
}

static void give(int id, int qty) {
    int got = inv_add(id, qty);
    if (got < qty) {
        drop_item(pl.x, pl.y, id, qty - got);
        msg("Your inventory is full, so it drops to the floor.");
    }
}

static void take(int id, int qty) {
    qty -= inv_remove(id, qty);
    for (int i = 0; i < EQ_COUNT && qty > 0; i++)
        if (pl.equip[i] == id) {
            pl.equip[i] = 0;
            qty--;
            gfx_set_player_palette();
        }
}

static void complete(int q) {
    const QuestDef *d = &quest_defs[q];
    if (quest_done(q)) return;
    pl.qstage[q] = d->done;
    for (int i = 0; i < d->nrewards; i++) {
        const QReward *r = &d->rewards[i];
        if (r->type == QR_XP) add_xp(r->skill, r->amount * 10);
        else if (r->type == QR_ITEM) give(r->item, r->amount);
    }
    if (npending < QUEST_COUNT) pending[npending++] = q;
}

// "You've spent 1 day, 4 hours, 12 minutes in the world..." wrapped into dialogue lines
static char playtime_text[160];
static const char *playtime_page(void) {
    u32 mins = pl.playtime / 3600;
    u32 d = mins / 1440, h = mins / 60 % 24, m = mins % 60;
    char buf[160], n[12];
    char *p = buf;
    const char *a = "You've spent ";
    while (*a) *p++ = *a++;
    const u32 v[3] = {d, h, m};
    const char *const unit[3] = {" day", " hour", " minute"};
    for (int i = 0; i < 3; i++) {
        fmt_int(n, v[i]);
        for (char *q = n; *q;) *p++ = *q++;
        for (const char *q = unit[i]; *q;) *p++ = *q++;
        if (v[i] != 1) *p++ = 's';
        *p++ = i < 1 ? ',' : i < 2 ? ' ' : ' ';
        if (i == 0) *p++ = ' ';
        if (i == 1) {
            *p++ = 'a';
            *p++ = 'n';
            *p++ = 'd';
            *p++ = ' ';
        }
    }
    a = "in the world since you first arrived.";
    while (*a) *p++ = *a++;
    *p = 0;
    char lines[3][48];
    int nl = text_wrap(buf, 172, lines, 3);
    p = playtime_text;
    for (int i = 0; i < nl; i++) {
        if (i) *p++ = '\n';
        for (char *q = lines[i]; *q;) *p++ = *q++;
    }
    *p = 0;
    return playtime_text;
}

int qvm_step(DlgEvent *ev) {
    int guard = 4000;
    while (vm.pc != VM_IDLE && guard--) {
        int pc = vm.pc;
        const u8 *c = &quest_code[pc];
        if (c[0] != OP_END && c[0] != OP_IF && c[0] != OP_GOTO) vm.acted = 1;
        switch (c[0]) {
        case OP_END: vm.pc = VM_IDLE; return 0;
        case OP_SAY:
            ev->type = DLG_SAY;
            ev->speaker = c[1] == 0xFF ? (vm.kind != 0xFF ? vm.kind : SPK_NONE) : c[1];
            ev->text = quest_str[rd16(pc + 2)];
            vm.pc = pc + 4;
            return 1;
        case OP_PLAYTIME:
            ev->type = DLG_SAY;
            ev->speaker = vm.kind != 0xFF ? vm.kind : SPK_NONE;
            ev->text = playtime_page();
            vm.pc = pc + 1;
            return 1;
        case OP_CHAT: msg_col(quest_str[rd16(pc + 2)], c[1]); vm.pc = pc + 4; break;
        case OP_CHOICE:
            ev->type = DLG_CHOICE;
            ev->nopt = c[1];
            for (int i = 0; i < c[1]; i++) {
                ev->opt[i] = quest_str[rd16(pc + 2 + i * 4)];
                vm.targets[i] = rd16(pc + 4 + i * 4);
            }
            return 1;
        case OP_GOTO: vm.pc = rd16(pc + 1); break;
        case OP_IF: {
            int t = cond(c[1], rd16(pc + 3), rd16(pc + 5)) ^ c[2];
            vm.pc = t ? rd16(pc + 7) : pc + 9;
            break;
        }
        case OP_STAGE: pl.qstage[c[1]] = c[2]; vm.pc = pc + 3; world_refresh_npcs(); break;
        case OP_FLAG: pl.qvar[c[1]] |= 1u << c[2]; vm.pc = pc + 3; world_refresh_npcs(); break;
        case OP_UNFLAG: pl.qvar[c[1]] &= ~(1u << c[2]); vm.pc = pc + 3; world_refresh_npcs(); break;
        case OP_ADDVAR: pl.qvar[c[1]] += c[2]; vm.pc = pc + 3; break;
        case OP_SETVAR: pl.qvar[c[1]] = rd16(pc + 2); vm.pc = pc + 4; break;
        case OP_GIVE: give(rd16(pc + 1), rd16(pc + 3)); vm.pc = pc + 5; break;
        case OP_TAKE: take(rd16(pc + 1), rd16(pc + 3)); vm.pc = pc + 5; break;
        case OP_XP: add_xp(c[1], rd16(pc + 2) * 10); vm.pc = pc + 4; break;
        case OP_SFX: sfx(c[1]); vm.pc = pc + 2; break;
        case OP_COMPLETE: complete(c[1]); vm.pc = pc + 2; world_refresh_npcs(); break;
        case OP_TP: player_tp(c[1], c[2], c[3]); vm.pc = pc + 4; break;
        case OP_WALKTHRU: player_walk_through(); vm.pc = pc + 1; break;
        case OP_NPCSTATE:
            if (vm.npc >= 0) npcs[vm.npc].state = c[1];
            vm.pc = pc + 2;
            break;
        case OP_NPCOFF:
            if (vm.npc >= 0) {
                npc_set_active(vm.npc, 0);
                npcs[vm.npc].respawn = 0;
            }
            vm.pc = pc + 1;
            break;
        case OP_NPCHEAL:
            if (vm.npc >= 0) npcs[vm.npc].hp = npc_defs[npcs[vm.npc].kind].hp;
            vm.pc = pc + 1;
            break;
        case OP_SHOP: vm.after = AFTER_SHOP; vm.pc = pc + 1; break;
        case OP_BANK: vm.after = AFTER_BANK; vm.pc = pc + 1; break;
        default: vm.pc = VM_IDLE; return 0;
        }
    }
    vm.pc = VM_IDLE;
    return 0;
}

void qvm_choose(int i) { vm.pc = vm.targets[i]; }

// 1 = open the shop, 2 = the bank, after the script that just finished
int qvm_after(void) {
    int a = vm.after;
    vm.after = AFTER_NONE;
    return a;
}

static int run(u16 entry, int npc) {
    if (entry == 0xFFFF) return 0;
    vm.pc = entry;
    vm.npc = npc;
    vm.kind = npc >= 0 ? npcs[npc].kind : 0xFF;
    vm.after = AFTER_NONE;
    vm.acted = 0;
    dialog_run_script();
    return vm.acted;   // 0: nothing happened, the caller may fall back to its default action
}

static u16 lookup(const QEntry *t, int key) {
    for (; t->key != 0xFFFF; t++)
        if (t->key == key) return t->entry;
    return 0xFFFF;
}

int quest_talk(int npc) { return run(quest_talk_entry[npcs[npc].kind], npc); }
int quest_object(int type, int x, int y) { return run(lookup(quest_obj_entry, type), -1); }
int quest_item_usable(int id) { return lookup(quest_item_entry, id) != 0xFFFF; }
int quest_item(int slot) { return run(lookup(quest_item_entry, pl.inv[slot].id), -1); }

int quest_npc_dying(int npc) {
    if (npcs[npc].kind == NPC_DELRITH && quest_stage(Q_DEMON) < quest_defs[Q_DEMON].done) {
        npcs[npc].hp = 1;
        npcs[npc].in_combat = 0;
        player_stop();
        return run(quest_event_entry[EV_DELRITH], npc);
    }
    return 0;
}

int quest_npc_visible(int kind) {
    int ctrl = 0;
    for (int i = 0; i < QSHOW_COUNT; i++) {
        const QShow *s = &quest_show[i];
        if (s->kind != kind) continue;
        ctrl = 1;
        int st = pl.qstage[s->q];
        if (st >= s->smin && st <= s->smax && (s->flag == 0xFF || ((pl.qvar[s->q] >> s->flag) & 1))) return 1;
    }
    return ctrl ? 0 : -1;
}
