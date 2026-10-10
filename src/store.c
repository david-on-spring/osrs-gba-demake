// Bank & shop screens (shop stock logic lives in shops.c).
#include "ui_internal.h"

static u8 tab, store_sel, store_scroll, inv_sel2, store_is_bank;
static const char *store_note;

static u8 cur_shop;
static Slot shop_slots[SHOP_SLOTS];     // view of the current shop's stock, refreshed per draw
#define SHOP_N (store_is_bank ? 96 : shop_count(cur_shop))

static void store_draw(void) {
    cv_clear(&cv_text);
    cv_clear(&cv_panel);
    icons_clear();
    if (!store_is_bank) shop_view(cur_shop, shop_slots);
    draw_header(store_is_bank ? "The Bank of Gielinor" : shop_name(cur_shop));
    // tabs
    // OSRS-style stone tabs; the open one is raised and lit
    const char *t0 = store_is_bank ? "Bank" : "Stock";
    int x = 120;
    for (int i = 0; i < 2; i++) {
        const char *t = i == 0 ? t0 : "Inventory";
        int w = text_width(t) + 12;
        draw_stone_button(x, 18, w, 12, 0);
        if (i == tab) cv_fill(&cv_panel, x + 2, 19, w - 4, 1, PC_GOLD);
        text_draw(x + 6, 20, t, i == tab ? TC_ORANGE : TC_DIM);
        x += w + 4;
    }
    draw_button(98, 20, 'L');
    draw_button(x, 20, 'R');
    // grid
    draw_panel(6, 31, 152, 91, 5);
    const Slot *slots;
    int count, sel, first = 0;
    if (tab == 0) {
        slots = store_is_bank ? pl.bank : shop_slots;
        count = SHOP_N;
        sel = store_sel;
        first = store_scroll * 7;
    } else {
        slots = pl.inv;
        count = 28;
        sel = inv_sel2;
    }
    // grid shifted down by 6 px vs. the items page
    for (int k = 0; k < 28; k++) {
        int i = first + k;
        int cx = GRID_X + (k % 7) * 20, cy = 36 + (k / 7) * 21;
        draw_cell(cx, cy, 19, 20, i == sel);
        if (i >= count || !slots[i].id) continue;
        icon_at(cx + 2, cy + 2, items[slots[i].id].icon);
        if (tab == 0 || slots[i].qty > 1 || (items[slots[i].id].flags & IF_STACK)) {
            // shop stock always shows its count, even 0 (as in OSRS); 1000+ as "1K"
            char n[12];
            if (slots[i].qty >= 1000) {
                char *e = sadd_num(n, slots[i].qty / 1000);
                e[0] = 'K';
                e[1] = 0;
            } else {
                fmt_int(n, slots[i].qty);
            }
            text_draw(cx + 1, cy, n, TC_YELLOW);
        }
    }
    if (sel >= first && sel < first + 28) {
        int k = sel - first;
        cursor_to(GRID_X + (k % 7) * 20 - 7, 36 + (k / 7) * 21 + 6);
    }
    if (tab == 0 && store_is_bank) {
        // scrollbar
        int rows = 14, vis = 4;
        cv_fill(&cv_panel, 153, 36, 2, 82, PC_BAR_BG);
        cv_fill(&cv_panel, 153, 36 + store_scroll * 82 / rows, 2, vis * 82 / rows, PC_EDGE);
    }
    draw_panel(162, 31, 72, 91, 5);
    const Slot *s = (sel < count) ? &slots[sel] : 0;
    int id = s ? s->id : 0;
    int pm = store_is_bank ? 0 : (tab == 0 ? 1 : 2);
    int price = !id || store_is_bank ? 0 : pm == 1 ? shop_buy_price(cur_shop, sel) : shop_sell_price(cur_shop, id);
    if (price < 0) {
        pm = 0;                           // this shop won't take it
        if (!store_note && id != I_COINS) store_note = "The shop won't buy that.";
    }
    draw_item_detail(id, (tab == 0 && !store_is_bank) ? 1 : (s ? s->qty : 0), 162, 31, 72, pm, price);
    draw_panel(6, 125, 228, 21, 4);
    char *p = sadd(sbuf, "Coins: ");
    p = sadd_num(p, inv_count(I_COINS));
    text_draw(14, 131, store_note ? store_note : sbuf, store_note ? TC_CYAN : TC_GOLD);
    if (store_is_bank) {
        draw_button(146, 131, 'S');
        text_draw(166, 131, "Deposit all", TC_GREY);
    }
    const char *a = store_is_bank ? (tab == 0 ? "Withdraw" : "Deposit") : (tab == 0 ? "Buy" : "Sell");
    draw_footer("L/R switch tabs", a, "Leave");
    if (pop.open) pop_draw();
}

static void store_open_common(int bank) {
    store_is_bank = bank;
    tab = 0;
    store_sel = store_scroll = inv_sel2 = 0;
    store_note = 0;
    pop.open = 0;
    game_state = bank ? GS_BANK : GS_SHOP;
    player_stop();
    cursor_hide();
    gfx_set_world_fade(7);
    gfx_set_mosaic(1);
    gfx_set_ui_alpha(13, 0);
    sfx(SFX_OK);
    store_draw();
}

void bank_open(void) { store_open_common(1); }
void shop_open(void) {
    cur_shop = shop_nearest();
    store_open_common(0);
}

static void store_leave(void) {
    sfx(SFX_BACK);
    game_state = GS_WORLD;
    pop.open = 0;
    cursor_hide();
    icons_clear();
    gfx_set_world_fade(16);
    gfx_set_mosaic(0);
    gfx_set_ui_alpha(12, 0);
    ui_hud_reset();
}

static void store_transact(int amount) {
    if (store_is_bank) {
        if (tab == 0) {
            Slot *b = &pl.bank[store_sel];
            if (!b->id) return;
            int n = amount > b->qty ? b->qty : amount;
            int added = inv_add(b->id, n);
            if (!added) {
                store_note = "Your inventory is full.";
                sfx(SFX_ERR);
                return;
            }
            b->qty -= added;
            if (!b->qty) b->id = 0;
        } else {
            Slot *s = &pl.inv[inv_sel2];
            if (!s->id) return;
            int id = s->id;
            int n = inv_count(id);
            if (n > amount) n = amount;
            if (bank_add(id, n)) inv_remove(id, n);
            else store_note = "Your bank is full.";
        }
        sfx(SFX_PICKUP);
    } else {
        if (tab == 0) {
            shop_view(cur_shop, shop_slots);
            int id = shop_slots[store_sel].id;
            if (!id) return;
            int got = 0;
            for (int k = 0; k < amount; k++) {
                // each item is priced at the stock left, so big buys get dearer
                if (!shop_slots[store_sel].qty || shop_slots[store_sel].id != id) {
                    store_note = "The shop has run out of stock.";
                    break;
                }
                int cost = shop_buy_price(cur_shop, store_sel);
                if (inv_count(I_COINS) < cost) {
                    store_note = "You don't have enough coins.";
                    break;
                }
                // paying with the whole coin stack frees its slot
                if (!inv_free() && !((items[id].flags & IF_STACK) && inv_count(id)) && inv_count(I_COINS) != cost) {
                    store_note = "You don't have enough inventory space.";
                    break;
                }
                inv_remove(I_COINS, cost);
                shop_take(cur_shop, store_sel);
                inv_add(id, 1);
                got++;
                shop_view(cur_shop, shop_slots);
            }
            sfx(got ? SFX_COINS : SFX_ERR);
        } else {
            Slot *s = &pl.inv[inv_sel2];
            int id = s->id;
            if (!id || id == I_COINS) return;
            int n = inv_count(id);
            if (n > amount) n = amount;
            int sold = 0;
            for (int k = 0; k < n; k++) {
                int gain = shop_sell_price(cur_shop, id);
                if (gain < 0) {
                    store_note = "The shop won't buy that.";
                    break;
                }
                if (!shop_put(cur_shop, id)) {
                    store_note = "The shop is full.";
                    break;
                }
                inv_remove(id, 1);
                if (gain) inv_add(I_COINS, gain);
                sold++;
            }
            sfx(sold ? SFX_COINS : SFX_ERR);
        }
    }
}

static void store_update(void) {
    if (pop.open) {
        int r = pop_input();
        if (r >= 0 && r < 100) store_transact(r);
        if (r != -1) store_draw();
        return;
    }
    int changed = 0;
    if (keys_pressed & (KEY_L | KEY_R)) {
        tab ^= 1;
        store_note = 0;
        sfx(SFX_MOVE);
        cursor_hide();
        changed = 1;
    }
    int count = tab == 0 ? (store_is_bank ? 96 : SHOP_N) : 28;
    int sel = tab == 0 ? store_sel : inv_sel2;
    int c = sel % 7, r = sel / 7;
    int rows = (count + 6) / 7;
    if (keys_repeat & KEY_LEFT) c = (c + 6) % 7;
    if (keys_repeat & KEY_RIGHT) c = (c + 1) % 7;
    if (keys_repeat & KEY_UP) r = (r + rows - 1) % rows;
    if (keys_repeat & KEY_DOWN) r = (r + 1) % rows;
    int ns = r * 7 + c;
    if (ns >= count) ns = count - 1;
    if (ns != sel) {
        sfx(SFX_MOVE);
        changed = 1;
        store_note = 0;
        if (tab == 0) {
            store_sel = ns;
            if (store_is_bank) {
                if (ns / 7 < store_scroll) store_scroll = ns / 7;
                if (ns / 7 >= store_scroll + 4) store_scroll = ns / 7 - 3;
            }
        } else {
            inv_sel2 = ns;
        }
    }
    if ((keys_pressed & KEY_SELECT) && store_is_bank) {
        for (int i = 0; i < 28; i++)
            if (pl.inv[i].id && bank_add(pl.inv[i].id, pl.inv[i].qty)) pl.inv[i].id = pl.inv[i].qty = 0;
        store_note = "You deposit your inventory.";
        sfx(SFX_PICKUP);
        changed = 1;
    }
    if (keys_pressed & KEY_A) {
        const Slot *s = tab == 0 ? (store_is_bank ? &pl.bank[store_sel] : &shop_slots[store_sel]) : &pl.inv[inv_sel2];
        if (s->id && !(tab == 1 && !store_is_bank && s->id == I_COINS)) {
            int k = tab == 0 ? store_sel : inv_sel2;
            pop_reset(GRID_X + (k % 7) * 20 + 22, 36 + ((k - (tab == 0 ? store_scroll * 7 : 0)) / 7) * 21);
            if (store_is_bank) {
                pop_add(tab == 0 ? "Withdraw 1" : "Deposit 1", 1);
                pop_add(tab == 0 ? "Withdraw 5" : "Deposit 5", 5);
                pop_add(tab == 0 ? "Withdraw All" : "Deposit All", 99);
            } else {
                // OSRS amounts: 1, 5, 10, 50
                pop_add(tab == 0 ? "Buy 1" : "Sell 1", 1);
                pop_add(tab == 0 ? "Buy 5" : "Sell 5", 5);
                pop_add(tab == 0 ? "Buy 10" : "Sell 10", 10);
                pop_add(tab == 0 ? "Buy 50" : "Sell 50", 50);
            }
            pop_add("Cancel", 200);
            sfx(SFX_OK);
            changed = 1;
        }
    }
    if (keys_pressed & (KEY_B | KEY_START)) {
        store_leave();
        return;
    }
    if (changed) store_draw();
}

void bank_update(void) { store_update(); }
void shop_update(void) { store_update(); }
