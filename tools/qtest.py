#!/usr/bin/env python3
"""Expand quest test scripts (.src) into harness scripts. usage: qtest.py in.src out.txt

Adds to the harness commands:
  tp X Y DIR             teleport (cells; DIR 0 down, 1 up, 2 left, 3 right)
  item SLOT NAME [QTY]   put an item (I_NAME) in inventory slot SLOT
  clearinv               empty the inventory
  equip SLOT NAME [QTY]  wear an item (SLOT = HEAD WEAPON BODY SHIELD LEGS FEET NECK AMMO; QTY for AMMO)
  spell NAME             set the autocast spell (a src/spells.c name in capitals, e.g. WIND_STRIKE; NONE)
  expectammo NAME N      PASS/FAIL check of the ammo slot and its quantity
  expectspell NAME       PASS/FAIL check of the autocast spell
  expectnpcat I|KIND [ZONE] X Y  PASS/FAIL check of an NPC's cell (with ZONE: an OSRS tile)
  expecthpbelow N        PASS/FAIL check that current hitpoints are below N
  homecd TICKS           set the home teleport cooldown
  expecthomecd MIN       PASS/FAIL check that the home teleport cooldown is at least MIN ticks
  expecttarget I|KIND|NONE  PASS/FAIL check of the L target (lock_target)
  expectcurse I|KIND BITS   PASS/FAIL check of an NPC's curse bits (CU_* in src/game.h)
  expectplace NAME       PASS/FAIL check that the player stands on a named cell from build/assets.h (e.g. TELE_VARROCK)
  stage QUEST N          set a quest stage (QUEST = id from tools/quests.txt)
  xp SKILL TENTHS        set a skill's XP (tenths)
  page [N]               let N dialogue pages type out and continue (default 1)
  choose K               pick option K (0-based) in a choice box
  newgame [NAME]         from the login screen: New User, type NAME (default A), first empty slot
  loadgame [N]           from the login screen: Existing User, slot N (default: first used)
  talk                   press A and give the dialogue a moment to open
  dumpq                  dump quest stages and vars
  dlg                    continue pages until a choice box, a popup or the end of the dialogue
  done | world           wait for the quest complete scroll / for the player to be free in the world
  ok [level]             dismiss the quest complete scroll (or a level-up popup)
  expectq QUEST N        PASS/FAIL check of a quest stage
  expectitem SLOT NAME   PASS/FAIL check of an inventory slot
  expecthas NAME         PASS/FAIL check that the item is anywhere in the inventory
  expectqty SLOT N       PASS/FAIL check of an inventory slot's quantity
  expectbank SLOT NAME   PASS/FAIL check of a bank slot
  expectequip SLOT NAME  PASS/FAIL check of a worn item (SLOT as for equip; NONE for empty)
  expectxp SKILL MIN     PASS/FAIL check that a skill has at least MIN XP (tenths)
  expecthp N             PASS/FAIL check of current hitpoints
  expectstate STATE      PASS/FAIL check of game_state (TITLE WORLD MENU DIALOG BANK SHOP LEVELUP QUESTDONE)
  hp N                   set current hitpoints
  npcpos I|KIND [ZONE] X Y  move NPC I (see build/spawns.txt), the first of a kind (or KIND:N, the Nth), to a cell
  untildead I|KIND [N]   run until that NPC is inactive (killed), at most N frames (default 3000)
  tpz ZONE X Y DIR       teleport to OSRS tile (X, Y) in a zone (OVERWORLD, CASTLE_1, CASTLE_2; a town's name
                         such as LUMBRIDGE is the overworld)
  expectat ZONE X Y      PASS/FAIL check that the player stands on OSRS tile (X, Y) of a zone (ZONE CELL: a cell)
  face THING DIR         stand next to THING facing it (DIR is the way the player faces; another free side
                         is used if that one is blocked). THING is an NPC kind (its first spawn), obj:TYPE
                         or item:NAME (the 'stand' lines in build/spawns.txt)
Player field offsets come from src/game.h's layout (checked against the map symbols)."""
import os
import re
import sys

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
hdr = open(os.path.join(root, 'src/game.h')).read()
# item ids from the enum in game.h
body = hdr[hdr.index('enum {\n    I_NONE'):hdr.index('I_COUNT')]
names = re.findall(r'I_(\w+)', body)
ITEM = {n: i for i, n in enumerate(names)}
QUEST = {}
for line in open(os.path.join(root, 'tools/quests.txt')):
    m = re.match(r'quest (\w+) ', line)
    if m:
        QUEST[m.group(1)] = len(QUEST)
SKILLS = ['ATTACK', 'STRENGTH', 'DEFENCE', 'HITPOINTS', 'PRAYER', 'WOODCUTTING', 'FIREMAKING', 'FISHING',
          'COOKING', 'MINING', 'RANGED', 'MAGIC']
EQ = ['HEAD', 'WEAPON', 'BODY', 'SHIELD', 'LEGS', 'FEET', 'NECK', 'AMMO']
# (src/save.c asserts these against the struct)
OFF_XP, OFF_INV, OFF_EQUIP, OFF_AMMO, OFF_BANK, OFF_QSTAGE, OFF_QVAR, OFF_SPELL, OFF_HOMECD = \
    0x20, 0x50, 0xC0, 0xD0, 0xD2, 0x258, 0x268, 0x288, 0x28A
STATES = ['TITLE', 'WORLD', 'MENU', 'DIALOG', 'BANK', 'SHOP', 'LEVELUP', 'QUESTDONE', 'TARGET']   # GS_* in src/game.h
NPC_SIZE = 30      # sizeof(Npc), asserted in src/world.c
# spell names in book order from src/spells.c ("Wind Strike" -> WIND_STRIKE)
SPELLS = [re.sub(r'\W+', '_', n).strip('_').upper()
          for n in re.findall(r'^\s+(?:\{|CB\(|CU\(|NA\()"([^"]+)"', open(os.path.join(root, 'src/spells.c')).read(), re.M)]

KB = ['ABCDEFGHIJKLM', 'NOPQRSTUVWXYZ', 'abcdefghijklm', 'nopqrstuvwxyz', '0123456789 -_']   # src/title.c kb_keys
SPAWNS_TXT = os.path.join(root, 'build/spawns.txt')


def spawn_lines():
    return [ln.split() for ln in open(SPAWNS_TXT)]


def zone_cell(z, X, Y):
    for p in spawn_lines():
        if p[0] == 'zone' and p[1] == z:
            OX, OY, S, CX0, CY0 = map(int, p[2:])
            return CX0 + (X - OX) // S, CY0 + (OY - Y) // S
    raise SystemExit('unknown zone ' + z)


def thing_cell(name):
    for p in spawn_lines():
        if (p[0].isdigit() and p[1] == name) or (p[0] in ('obj', 'item', 'place') and
                                                  (p[0] + ':' + p[1] == name or (p[0] == 'place' and p[1] == name))):
            return int(p[-2]), int(p[-1])
    raise SystemExit('unknown thing ' + name)


def at_arg(p, i):
    """Cell from p[i:]: 'X Y' cells, or 'ZONE X Y' in OSRS tiles."""
    if p[i].isalpha():
        return zone_cell(p[i], int(p[i + 1]), int(p[i + 2]))
    return int(p[i]), int(p[i + 1])


def tp_lines(x, y, d):
    return ['press B', 'wait 2', 'poke16 @pl %x' % x, 'poke16 @pl+2 %x' % y, 'poke16 @pl+4 %x' % (x * 16),
            'poke16 @pl+6 %x' % (y * 16), 'poke8 @pl+8 %x' % d, 'wait 4']


DIRV = {'DOWN': (0, 0, 1), 'UP': (1, 0, -1), 'LEFT': (2, -1, 0), 'RIGHT': (3, 1, 0)}


def npc_index(k):          # an index, a kind's first spawn, or KIND:N, its Nth (indices shift as NPCs are added)
    if k.isdigit():
        return int(k)
    kind, _, nth = k.partition(':')
    hits = [int(ln.split()[0]) for ln in open(os.path.join(root, 'build/spawns.txt'))
            if ln.split()[0].isdigit() and ln.split()[1] == kind]
    return hits[int(nth) - 1 if nth else 0]


out = []
for line in open(sys.argv[1]):
    p = line.split()
    if not p:
        continue
    c = p[0]
    if c == 'tp':
        x, y, d = int(p[1]), int(p[2]), int(p[3])
        out += ['press B', 'wait 2', 'poke16 @pl %x' % x, 'poke16 @pl+2 %x' % y, 'poke16 @pl+4 %x' % (x * 16),
                'poke16 @pl+6 %x' % (y * 16), 'poke8 @pl+8 %x' % d, 'wait 4']
    elif c == 'tpz':
        x, y = zone_cell(p[1], int(p[2]), int(p[3]))
        out += tp_lines(x, y, int(p[4]) if p[4].isdigit() else DIRV[p[4]][0])
    elif c == 'expectat':
        x, y = (int(p[2]), int(p[3])) if p[1] == 'CELL' else zone_cell(p[1], int(p[2]), int(p[3]))
        out += ['expect16 @pl %x %s_x' % (x, c), 'expect16 @pl+2 %x %s_y' % (y, c)]
    elif c == 'face':       # stand beside the thing facing it, from the side asked for if it is free
        d = DIRV[p[2]][0]
        st = [(int(q[2]), int(q[3]), int(q[4])) for q in spawn_lines() if q[0] == 'stand' and q[1] == p[1]]
        if not st:
            raise SystemExit('nowhere to stand facing ' + p[1])
        sx, sy, sd = next((t for t in st if t[2] == d), st[0])
        out += tp_lines(sx, sy, sd)
    elif c == 'item':
        slot, it = int(p[1]), ITEM[p[2]]
        q = int(p[3]) if len(p) > 3 else 1
        out += ['poke16 @pl+0x%x %x' % (OFF_INV + slot * 4, it), 'poke16 @pl+0x%x %x' % (OFF_INV + slot * 4 + 2, q)]
    elif c == 'clearinv':
        for s in range(28):
            out += ['poke16 @pl+0x%x 0' % (OFF_INV + s * 4), 'poke16 @pl+0x%x 0' % (OFF_INV + s * 4 + 2)]
    elif c == 'equip':
        out.append('poke16 @pl+0x%x %x' % (OFF_EQUIP + EQ.index(p[1]) * 2, ITEM[p[2]]))
        if p[1] == 'AMMO':
            out.append('poke16 @pl+0x%x %x' % (OFF_AMMO, int(p[3]) if len(p) > 3 else 1))
    elif c == 'spell':
        out.append('poke8 @pl+0x%x %x' % (OFF_SPELL, SPELLS.index(p[1])))
    elif c == 'expectammo':
        out += ['expect16 @pl+0x%x %x ammo_%s' % (OFF_EQUIP + EQ.index('AMMO') * 2, ITEM[p[1]], p[1]),
                'expect16 @pl+0x%x %x ammo_qty_%s' % (OFF_AMMO, int(p[2]), p[2])]
    elif c == 'expecttarget':
        v = 0xFFFF if p[1] == 'NONE' else npc_index(p[1])
        out.append('expect16 @lock_target %x target_%s' % (v, p[1]))
    elif c == 'expectcurse':
        out.append('expect8 @npcs+0x%x %x curse_%s' % (npc_index(p[1]) * NPC_SIZE + 28, int(p[2]), p[1]))
    elif c == 'expectplace':
        hdr_a = open(os.path.join(root, 'build/assets.h')).read()
        x = int(re.search(r'#define %s_X (\d+)' % p[1], hdr_a).group(1))
        y = int(re.search(r'#define %s_Y (\d+)' % p[1], hdr_a).group(1))
        out += ['expect16 @pl %x %s_x' % (x, p[1]), 'expect16 @pl+2 %x %s_y' % (y, p[1])]
    elif c == 'expectnpcat':
        base = npc_index(p[1]) * NPC_SIZE
        x, y = at_arg(p, 2)
        out += ['expect16 @npcs+0x%x %x %s_x' % (base + 4, x, p[1]),
                'expect16 @npcs+0x%x %x %s_y' % (base + 6, y, p[1])]
    elif c == 'expecthpbelow':
        out.append('expectlt8 @pl+0x16 %x hp_below_%s' % (int(p[1]), p[1]))
    elif c == 'homecd':
        out.append('poke16 @pl+0x%x %x' % (OFF_HOMECD, int(p[1])))
    elif c == 'expecthomecd':
        out.append('expectge32 @pl+0x%x %x homecd_ge_%s' % (OFF_HOMECD, int(p[1]), p[1]))
    elif c == 'expectspell':
        out.append('expect8 @pl+0x%x %x spell_%s' % (OFF_SPELL, SPELLS.index(p[1]), p[1]))
    elif c == 'stage':
        out.append('poke8 @pl+0x%x %x' % (OFF_QSTAGE + QUEST[p[1]], int(p[2])))
    elif c == 'xp':
        v = int(p[2])
        k = SKILLS.index(p[1])
        out += ['poke16 @pl+0x%x %x' % (OFF_XP + k * 4, v & 0xFFFF), 'poke16 @pl+0x%x %x' % (OFF_XP + k * 4 + 2, v >> 16)]
    elif c == 'page':
        for _ in range(int(p[1]) if len(p) > 1 else 1):
            out += ['until8 @ui_wait 1 900', 'press A']
    elif c == 'dlg':        # continue pages until a choice box, a popup or the end of the dialogue
        out.append('dlgrun @ui_wait 4000')
    elif c == 'choose':
        out += ['until8 @ui_wait 2 900', 'wait 4']
        out += ['press DOWN'] * int(p[1])
        out += ['press A', 'wait 2']
    elif c == 'done':       # the quest complete scroll is showing
        out += ['until8 @ui_wait 3 900', 'wait 10']
    elif c == 'world':      # back in the world, free to act
        out += ['until8 @ui_wait 5 900', 'wait 4']
    elif c == 'expectq':    # expectq QUEST STAGE
        out.append('expect8 @pl+0x%x %x %s_stage_%s' % (OFF_QSTAGE + QUEST[p[1]], int(p[2]), p[1], p[2]))
    elif c == 'expectitem':  # expectitem SLOT NAME
        out.append('expect16 @pl+0x%x %x slot%s_%s' % (OFF_INV + int(p[1]) * 4, ITEM[p[2]], p[1], p[2]))
    elif c == 'ok':         # dismiss a popup (quest complete / level up) once it is up
        out += ['until8 @ui_wait %d 900' % (4 if len(p) > 1 and p[1] == 'level' else 3), 'wait 10', 'press A', 'wait 4']
    elif c == 'expecthas':  # expecthas NAME: the item is somewhere in the inventory
        out.append('expectinv @pl+0x%x %x has_%s' % (OFF_INV, ITEM[p[1]], p[1]))
    elif c == 'expectqty':  # expectqty SLOT N
        out.append('expect16 @pl+0x%x %x slot%s_qty_%s' % (OFF_INV + int(p[1]) * 4 + 2, int(p[2]), p[1], p[2]))
    elif c == 'expectbank':  # expectbank SLOT NAME
        out.append('expect16 @pl+0x%x %x bank%s_%s' % (OFF_BANK + int(p[1]) * 4, ITEM[p[2]], p[1], p[2]))
    elif c == 'expectequip':  # expectequip SLOT NAME
        out.append('expect16 @pl+0x%x %x equip_%s_%s' % (OFF_EQUIP + EQ.index(p[1]) * 2, ITEM[p[2]], p[1], p[2]))
    elif c == 'expectxp':   # expectxp SKILL MIN
        out.append('expectge32 @pl+0x%x %x %s_xp_ge_%s' % (OFF_XP + SKILLS.index(p[1]) * 4, int(p[2]), p[1], p[2]))
    elif c == 'expecthp':
        out.append('expect8 @pl+0x16 %x hp_%s' % (int(p[1]), p[1]))
    elif c == 'expectstate':
        out.append('expect8 @game_state %x state_%s' % (STATES.index(p[1]), p[1]))
    elif c == 'npcpos':     # npcpos INDEX X Y: put an NPC (index from build/spawns.txt) on a cell
        x, y = at_arg(p, 2)
        base = npc_index(p[1]) * NPC_SIZE
        cell = y * next(int(q[1]) for q in spawn_lines() if q[0] == 'mapw') + x
        out += ['poke8 @npcs+0x%x 0' % (base + 3), 'poke16 @npcs+0x%x %x' % (base + 4, x), 'poke16 @npcs+0x%x %x' % (base + 6, y),
                'poke16 @npcs+0x%x %x' % (base + 8, x * 16), 'poke16 @npcs+0x%x %x' % (base + 10, y * 16),
                'poke8 @occ_bits+0x%x %x' % ((cell >> 5) * 4 + ((cell & 31) >> 3), 1 << (cell & 7))]
    elif c == 'untildead':
        out.append('until8 @npcs+0x%x 0 %d' % (npc_index(p[1]) * NPC_SIZE, int(p[2]) if len(p) > 2 else 3000))
    elif c == 'hp':
        out.append('poke8 @pl+0x16 %x' % int(p[1]))
    elif c == 'newgame':    # login screen: New User, name "A" (or the given name), first empty slot
        out += ['press A', 'wait 6']
        for ch in (p[1] if len(p) > 1 else 'A'):
            k = next((r, row.index(ch)) for r, row in enumerate(KB) if ch in row)
            out += ['press DOWN'] * k[0] + ['press RIGHT'] * k[1] + ['press A'] + ['press UP'] * k[0] + ['press LEFT'] * k[1]
        out += ['press START', 'wait 6', 'press A']
    elif c == 'loadgame':   # login screen: Existing User, slot N (default: the first used one)
        out += ['press A', 'wait 6'] + ['press DOWN'] * (int(p[1]) - 1 if len(p) > 1 else 0) + ['press A']
    elif c == 'talk':
        out += ['press A', 'wait 4']
    elif c == 'dumpq':
        out += ['dump @pl+0x%x 8' % OFF_QSTAGE, 'dump @pl+0x%x 16' % OFF_QVAR]
    else:
        out.append(line.rstrip('\n'))
open(sys.argv[2], 'w').write('\n'.join(out) + '\n')
