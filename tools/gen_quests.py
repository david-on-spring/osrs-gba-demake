#!/usr/bin/env python3
"""Quest compiler: turns tools/quests.txt (quest definitions + dialogue scripts) into
build/quest_data.c / quest_data.h for src/quests.c.

Called from gen_assets.py (which owns the font metrics and the world): emit(outdir, font_w, world_gen).

quests.txt is line based ('#' starts a comment):

  quest ID "Name"              begin a quest definition (journal order = file order)
    qp N / done N / reqqp N    quest points, completion stage, quest points needed to start
    icon ITEM                  item icon on the completion scroll
    start "text"               journal text before the quest is started
    step N "text"              journal text for stage N (1 .. done-1)
    need N ITEM QTY [flag B | var]   item checklist shown in the journal at stage N
    reward xp SKILL N | reward item ITEM N | reward text "line"
  end

  talk NPC | obj TYPE | item ITEM | event NAME     start a script (ends at the next block)
  show NPC QUEST MIN MAX [flag B]                  NPC only exists while MIN <= stage <= MAX

Script statements:
  :label
  npc "text" | player "text" | narrate "text" | say NPC "text"      dialogue box (auto-paged)
  chat "text" | chatc COLOUR "text"                                  HUD message, no box
  choice  followed by lines   option "text" label|end
  goto label | end
  if [not] COND goto label
      COND: stage Q OP N (OP: == != >= <= > <) | done Q | started Q | has ITEM [N] | worn ITEM
            flag Q B | var Q >= N | near POS R | near X Y R | qp >= N | free N | chance PCT
            facing DIR | level SKILL >= N | npcstate
  stage Q N | flag Q B | unflag Q B | addvar Q N | setvar Q N
  give ITEM [N] | take ITEM [N] | xp SKILL N | sfx NAME | complete Q
  tp X Y DIR | walkthrough | npcstate N | npcoff | npcheal | shop | bank
"""
import os
import re
import shlex

OPS = ['END', 'SAY', 'CHAT', 'CHOICE', 'GOTO', 'IF', 'STAGE', 'FLAG', 'UNFLAG', 'GIVE', 'TAKE', 'XP', 'SFX',
       'COMPLETE', 'TP', 'ADDVAR', 'NPCSTATE', 'NPCOFF', 'NPCHEAL', 'WALKTHRU', 'SHOP', 'BANK', 'SETVAR']
OP = {n: i for i, n in enumerate(OPS)}
CONDS = ['STAGE_EQ', 'STAGE_NE', 'STAGE_GE', 'STAGE_LE', 'STAGE_GT', 'STAGE_LT', 'HAS', 'WORN', 'FLAG', 'VAR_GE',
         'NEAR', 'QP_GE', 'FREE', 'CHANCE', 'DONE', 'FACING', 'LEVEL_GE', 'NPCSTATE']
COND = {n: i for i, n in enumerate(CONDS)}
STAGE_OPS = {'==': 'STAGE_EQ', '!=': 'STAGE_NE', '>=': 'STAGE_GE', '<=': 'STAGE_LE', '>': 'STAGE_GT', '<': 'STAGE_LT'}
DIRS = {'DOWN': 0, 'UP': 1, 'LEFT': 2, 'RIGHT': 3}
COLOURS = {'WHITE': 'TC_WHITE', 'GOLD': 'TC_GOLD', 'CYAN': 'TC_CYAN', 'GREEN': 'TC_GREEN', 'RED': 'TC_RED',
           'YELLOW': 'TC_YELLOW', 'ORANGE': 'TC_ORANGE'}
SPK_TARGET, SPK_PLAYER, SPK_NONE = 0xFF, 0xFE, 0xFD
PAGE_W = 172          # dialogue text width in pixels (see ui.c)
PAGE_LINES = 3
OPTION_W = 196


class QuestError(Exception):
    pass


class Compiler:
    def __init__(self, font_w, world):
        self.font_w = font_w
        self.world = world
        self.quests = []           # dicts
        self.qindex = {}
        self.code = []             # list of C expressions (one byte each)
        self.labels = {}           # global label -> offset
        self.fixups = []           # (code index, label, block, line)
        self.strings = []
        self.str_index = {}
        self.talk = {}             # NPC -> offset
        self.obj = {}
        self.item = {}
        self.event = {}
        self.show = []
        self.alias = {}
        self.block = None
        self.block_id = 0

    # ---- helpers -------------------------------------------------------------------------
    def width(self, s):
        w = 0
        for ch in s:
            o = ord(ch) - 32
            if o < 0 or o >= 96:
                raise QuestError('unsupported character %r in %r' % (ch, s))
            w += self.font_w[o] + 1
        return w - 1 if w else 0

    def wrap(self, text):
        words = text.split(' ')
        lines, cur = [], ''
        for wd in words:
            t = wd if not cur else cur + ' ' + wd
            if self.width(t) <= PAGE_W:
                cur = t
            else:
                if not cur:
                    raise QuestError('word too wide: %r' % wd)
                lines.append(cur)
                cur = wd
        if cur:
            lines.append(cur)
        return lines

    def pages(self, text):
        lines = self.wrap(text)
        return ['\n'.join(lines[i:i + PAGE_LINES]) for i in range(0, len(lines), PAGE_LINES)]

    def s(self, text):
        if text not in self.str_index:
            self.str_index[text] = len(self.strings)
            self.strings.append(text)
        return self.str_index[text]

    def emit(self, *vals):
        self.code.extend(str(v) for v in vals)

    def u16(self, v):
        if isinstance(v, int):
            self.emit(v & 0xFF, (v >> 8) & 0xFF)
        else:
            self.emit('(%s) & 0xFF' % v, '(%s) >> 8' % v)

    def target(self, label, line):
        if label == 'end':
            self.u16(0xFFFF)
            return
        self.fixups.append((len(self.code), '%d:%s' % (self.block_id, label), line))
        self.emit(0, 0)

    def quest(self, name, line):
        if name not in self.qindex:
            raise QuestError('line %d: unknown quest %s' % (line, name))
        return self.qindex[name]

    def pos(self, args, line):
        """near POS R | near X Y R -> (x, y, r)"""
        if len(args) == 2:
            if args[0] not in self.world.QPOS:
                raise QuestError('line %d: unknown position %s' % (line, args[0]))
            x, y = self.world.QPOS[args[0]]
            return x, y, int(args[1])
        return int(args[0]), int(args[1]), int(args[2])

    # ---- parsing --------------------------------------------------------------------------
    def parse(self, path):
        raw = open(path).read().split('\n')
        i = 0
        q = None
        while i < len(raw):
            ln = i + 1
            line = raw[i].strip()
            i += 1
            if not line or line.startswith('#'):
                continue
            try:
                tok = shlex.split(line, comments=False)
            except ValueError as e:
                raise QuestError('line %d: %s' % (ln, e))
            kw = tok[0]
            if q is not None:
                if kw == 'end':
                    self.finish_quest(q, ln)
                    q = None
                elif kw == 'qp':
                    q['qp'] = int(tok[1])
                elif kw == 'done':
                    q['done'] = int(tok[1])
                elif kw == 'reqqp':
                    q['reqqp'] = int(tok[1])
                elif kw == 'icon':
                    q['icon'] = tok[1]
                elif kw == 'start':
                    q['journal'][0] = tok[1]
                elif kw == 'step':
                    q['journal'][int(tok[1])] = tok[2]
                elif kw == 'need':
                    st, it, n = int(tok[1]), tok[2], int(tok[3])
                    fl = 0xFF
                    if len(tok) > 4:
                        fl = 0xFE if tok[4] == 'var' else int(tok[5])
                    q['needs'].append((st, it, n, fl))
                elif kw == 'reward':
                    q['rewards'].append(tok[1:])
                else:
                    raise QuestError('line %d: unknown quest field %s' % (ln, kw))
                continue
            if kw == 'quest':
                q = {'id': tok[1], 'name': tok[2], 'qp': 1, 'done': None, 'reqqp': 0, 'icon': None,
                     'journal': {}, 'needs': [], 'rewards': [], 'line': ln}
                self.qindex[tok[1]] = len(self.quests)
                self.quests.append(q)
                continue
            if kw == 'talk' and len(tok) == 4 and tok[2] == '=':
                self.end_block()
                self.alias[tok[1]] = tok[3]
                continue
            if kw in ('talk', 'obj', 'item', 'event'):
                self.end_block()
                self.block_id += 1
                table = {'talk': self.talk, 'obj': self.obj, 'item': self.item, 'event': self.event}[kw]
                if tok[1] in table:
                    raise QuestError('line %d: duplicate %s %s' % (ln, kw, tok[1]))
                table[tok[1]] = len(self.code)
                self.block = (kw, tok[1])
                continue
            if kw == 'show':
                self.end_block()
                fl = 0xFF
                if len(tok) > 5 and tok[5] == 'flag':
                    fl = int(tok[6])
                self.show.append((tok[1], self.quest(tok[2], ln), int(tok[3]), int(tok[4]), fl))
                continue
            if self.block is None:
                raise QuestError('line %d: statement outside a script: %s' % (ln, line))
            if kw == 'choice':
                opts = []
                while i < len(raw):
                    t2 = raw[i].strip()
                    if not t2 or t2.startswith('#'):
                        i += 1
                        continue
                    tt = shlex.split(t2)
                    if tt[0] != 'option':
                        break
                    opts.append((tt[1], tt[2], i + 1))
                    i += 1
                self.stmt_choice(opts, ln)
                continue
            self.stmt(tok, ln)
        self.end_block()
        if q is not None:
            raise QuestError('unterminated quest %s' % q['id'])

    def end_block(self):
        if self.block is not None:
            self.emit('OP_END')
            self.block = None

    def finish_quest(self, q, ln):
        if q['done'] is None:
            raise QuestError('quest %s: missing done' % q['id'])
        for st in range(0, q['done']):
            if st not in q['journal']:
                raise QuestError('quest %s: no journal text for stage %d' % (q['id'], st))

    def say(self, spk, text, ln):
        for pg in self.pages(text):
            self.emit('OP_SAY', spk)
            self.u16(self.s(pg))

    def stmt_choice(self, opts, ln):
        if not 2 <= len(opts) <= 5:
            raise QuestError('line %d: a choice needs 2..5 options' % ln)
        self.emit('OP_CHOICE', len(opts))
        for (text, label, l2) in opts:
            if self.width(text) > OPTION_W:
                raise QuestError('line %d: option too wide: %r' % (l2, text))
            self.u16(self.s(text))
            self.target(label, l2)

    def stmt(self, t, ln):
        kw = t[0]
        if kw.startswith(':'):
            key = '%d:%s' % (self.block_id, kw[1:])
            if key in self.labels:
                raise QuestError('line %d: duplicate label %s' % (ln, kw))
            self.labels[key] = len(self.code)
        elif kw == 'npc':
            self.say(SPK_TARGET, t[1], ln)
        elif kw == 'player':
            self.say(SPK_PLAYER, t[1], ln)
        elif kw == 'narrate':
            self.say(SPK_NONE, t[1], ln)
        elif kw == 'say':
            self.say('NPC_' + t[1], t[2], ln)
        elif kw == 'chat':
            if self.width(t[1]) > 144 * 3:
                raise QuestError('line %d: chat message too long' % ln)
            self.emit('OP_CHAT', 'TC_WHITE')
            self.u16(self.s(t[1]))
        elif kw == 'chatc':
            self.emit('OP_CHAT', COLOURS[t[1]])
            self.u16(self.s(t[2]))
        elif kw == 'goto':
            self.emit('OP_GOTO')
            self.target(t[1], ln)
        elif kw == 'end':
            self.emit('OP_END')
        elif kw == 'if':
            self.stmt_if(t, ln)
        elif kw == 'stage':
            self.emit('OP_STAGE', 'Q_' + t[1], int(t[2]))
            self.quest(t[1], ln)
        elif kw in ('flag', 'unflag'):
            self.emit('OP_FLAG' if kw == 'flag' else 'OP_UNFLAG', 'Q_' + t[1], int(t[2]))
        elif kw == 'addvar':
            self.emit('OP_ADDVAR', 'Q_' + t[1], int(t[2]))
        elif kw == 'setvar':
            self.emit('OP_SETVAR', 'Q_' + t[1])
            self.u16(int(t[2]))
        elif kw in ('give', 'take'):
            self.emit('OP_GIVE' if kw == 'give' else 'OP_TAKE')
            self.u16('I_' + t[1])
            self.u16(int(t[2]) if len(t) > 2 else 1)
        elif kw == 'xp':
            self.emit('OP_XP', 'SK_' + t[1])
            self.u16(int(t[2]))
        elif kw == 'sfx':
            self.emit('OP_SFX', 'SFX_' + t[1])
        elif kw == 'complete':
            self.quest(t[1], ln)
            self.emit('OP_COMPLETE', 'Q_' + t[1])
        elif kw == 'tp':
            self.emit('OP_TP', int(t[1]), int(t[2]), DIRS[t[3]])
        elif kw == 'walkthrough':
            self.emit('OP_WALKTHRU')
        elif kw == 'npcstate':
            self.emit('OP_NPCSTATE', int(t[1]))
        elif kw == 'npcoff':
            self.emit('OP_NPCOFF')
        elif kw == 'npcheal':
            self.emit('OP_NPCHEAL')
        elif kw == 'shop':
            self.emit('OP_SHOP')
        elif kw == 'bank':
            self.emit('OP_BANK')
        else:
            raise QuestError('line %d: unknown statement %s' % (ln, kw))

    def stmt_if(self, t, ln):
        if t[-2] != 'goto':
            raise QuestError('line %d: if ... goto label' % ln)
        label = t[-1]
        c = t[1:-2]
        neg = 0
        if c[0] == 'not':
            neg = 1
            c = c[1:]
        k = c[0]
        a, b = 0, 0
        if k == 'stage':
            cond = STAGE_OPS[c[2]]
            self.quest(c[1], ln)
            a, b = 'Q_' + c[1], int(c[3])
        elif k == 'done':
            cond, a = 'DONE', 'Q_' + c[1]
            self.quest(c[1], ln)
        elif k == 'started':
            cond, a, b = 'STAGE_GE', 'Q_' + c[1], 1
            self.quest(c[1], ln)
        elif k == 'has':
            cond, a, b = 'HAS', 'I_' + c[1], int(c[2]) if len(c) > 2 else 1
        elif k == 'worn':
            cond, a = 'WORN', 'I_' + c[1]
        elif k == 'flag':
            cond, a, b = 'FLAG', 'Q_' + c[1], int(c[2])
        elif k == 'var':
            assert c[2] == '>='
            cond, a, b = 'VAR_GE', 'Q_' + c[1], int(c[3])
        elif k == 'near':
            x, y, r = self.pos(c[1:], ln)
            cond, a, b = 'NEAR', x | (y << 8), r
        elif k == 'qp':
            cond, b = 'QP_GE', int(c[2])
        elif k == 'free':
            cond, b = 'FREE', int(c[1])
        elif k == 'chance':
            cond, b = 'CHANCE', int(c[1])
        elif k == 'facing':
            cond, b = 'FACING', DIRS[c[1]]
        elif k == 'level':
            assert c[2] == '>='
            cond, a, b = 'LEVEL_GE', 'SK_' + c[1], int(c[3])
        elif k == 'npcstate':
            cond = 'NPCSTATE'
        else:
            raise QuestError('line %d: unknown condition %s' % (ln, k))
        self.emit('OP_IF', 'C_' + cond, neg)
        self.u16(a)
        self.u16(b)
        self.target(label, ln)

    def link(self):
        for k, v in self.alias.items():
            if v not in self.talk:
                raise QuestError('talk %s = %s: no such script' % (k, v))
            self.talk[k] = self.talk[v]
        for (idx, key, ln) in self.fixups:
            if key not in self.labels:
                raise QuestError('line %d: undefined label %s' % (ln, key.split(':', 1)[1]))
            off = self.labels[key]
            self.code[idx] = str(off & 0xFF)
            self.code[idx + 1] = str(off >> 8)
        if len(self.code) >= 0xFFFF:
            raise QuestError('quest bytecode too large')

    # ---- output --------------------------------------------------------------------------
    def write(self, outdir, npc_kinds, item_spawns):
        def cstr(s):
            return '"' + s.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n') + '"'
        h = ['// generated by tools/gen_quests.py from tools/quests.txt - do not edit', '#pragma once', '']
        for i, q in enumerate(self.quests):
            h.append('#define Q_%s %d' % (q['id'], i))
        h.append('#define QUEST_COUNT %d' % len(self.quests))
        for i, n in enumerate(OPS):
            h.append('#define OP_%s %d' % (n, i))
        for i, n in enumerate(CONDS):
            h.append('#define C_%s %d' % (n, i))
        evs = sorted(self.event)
        for i, n in enumerate(evs):
            h.append('#define EV_%s %d' % (n, i))
        h.append('#define EV_COUNT %d' % len(evs))
        h.append('#define QOBJ_COUNT %d' % len(self.obj))
        h.append('#define QITEM_COUNT %d' % len(self.item))
        h.append('#define QSHOW_COUNT %d' % len(self.show))
        h.append('#define ITEM_SPAWN_COUNT %d' % len(item_spawns))
        h.append('''
enum { QR_XP, QR_ITEM, QR_TEXT };
typedef struct { u16 item; u8 qty, flag; } QNeed;            // flag: bit to show as handed in, 0xFE = counter, 0xFF none
typedef struct { u8 type, skill; u16 item, amount; const char *text; } QReward;
typedef struct {
    const char *name;
    u8 qp, done, need_stage, nneeds, nrewards, req_qp;
    u16 icon;
    const char *const *journal;     // [0] = how to start, [1..done-1] = steps
    const QNeed *needs;
    const QReward *rewards;
} QuestDef;
typedef struct { u16 key, entry; } QEntry;
typedef struct { u8 kind, q, smin, smax, flag; } QShow;
typedef struct { u16 item; u8 x, y; } ItemSpawn;
extern const QuestDef quest_defs[QUEST_COUNT];
extern const u8 quest_code[];
extern const char *const quest_str[];
extern const u16 quest_talk_entry[NPC_KIND_COUNT];
extern const QEntry quest_obj_entry[QOBJ_COUNT + 1];
extern const QEntry quest_item_entry[QITEM_COUNT + 1];
extern const u16 quest_event_entry[EV_COUNT + 1];
extern const QShow quest_show[QSHOW_COUNT + 1];
extern const ItemSpawn item_spawns[ITEM_SPAWN_COUNT + 1];''')
        c = ['// generated by tools/gen_quests.py from tools/quests.txt - do not edit', '#include "game.h"', '']
        c.append('const char *const quest_str[] = {')
        for st in self.strings:
            c.append('  %s,' % cstr(st))
        c.append('};')
        c.append('const u8 quest_code[] = {')
        for k in range(0, len(self.code), 12):
            c.append('  ' + ', '.join(self.code[k:k + 12]) + ',')
        c.append('};')
        for i, q in enumerate(self.quests):
            c.append('static const char *const qj_%d[] = {%s};' % (
                i, ', '.join(cstr(q['journal'][s]) for s in range(q['done']))))
            needs = q['needs']
            stages = set(n[0] for n in needs)
            if len(stages) > 1:
                raise QuestError('quest %s: needs must all be for one stage' % q['id'])
            c.append('static const QNeed qn_%d[] = {%s};' % (
                i, ', '.join('{I_%s, %d, %d}' % (it, n, fl) for (_, it, n, fl) in needs) or '{0, 0, 0}'))
            rw = []
            for r in q['rewards']:
                if r[0] == 'xp':
                    rw.append('{QR_XP, SK_%s, 0, %d, 0}' % (r[1], int(r[2])))
                elif r[0] == 'item':
                    rw.append('{QR_ITEM, 0, I_%s, %d, 0}' % (r[1], int(r[2])))
                elif r[0] == 'text':
                    rw.append('{QR_TEXT, 0, 0, 0, %s}' % cstr(r[1]))
                else:
                    raise QuestError('quest %s: bad reward %r' % (q['id'], r))
            c.append('static const QReward qr_%d[] = {%s};' % (i, ', '.join(rw) or '{0, 0, 0, 0, 0}'))
        c.append('const QuestDef quest_defs[QUEST_COUNT] = {')
        for i, q in enumerate(self.quests):
            ns = q['needs'][0][0] if q['needs'] else 0xFF
            c.append('  {%s, %d, %d, %d, %d, %d, %d, I_%s, qj_%d, qn_%d, qr_%d},' % (
                cstr(q['name']), q['qp'], q['done'], ns, len(q['needs']), len(q['rewards']), q['reqqp'],
                q['icon'] or 'COINS', i, i, i))
        c.append('};')
        c.append('const u16 quest_talk_entry[NPC_KIND_COUNT] = {')
        for k in npc_kinds:
            c.append('  [NPC_%s] = %s,' % (k, self.talk.get(k, '0xFFFF')))
        c.append('};')
        for k in self.talk:
            if k not in npc_kinds:
                raise QuestError('talk: unknown NPC %s' % k)
        c.append('const QEntry quest_obj_entry[QOBJ_COUNT + 1] = {%s{0xFFFF, 0}};' % ''.join(
            '{T_%s, %d}, ' % (k, v) for k, v in self.obj.items()))
        c.append('const QEntry quest_item_entry[QITEM_COUNT + 1] = {%s{0xFFFF, 0}};' % ''.join(
            '{I_%s, %d}, ' % (k, v) for k, v in self.item.items()))
        c.append('const u16 quest_event_entry[EV_COUNT + 1] = {%s0xFFFF};' % ''.join(
            '%d, ' % self.event[k] for k in evs))
        c.append('const QShow quest_show[QSHOW_COUNT + 1] = {%s{0, 0, 0, 0, 0}};' % ''.join(
            '{NPC_%s, %d, %d, %d, %d}, ' % s for s in self.show))
        c.append('const ItemSpawn item_spawns[ITEM_SPAWN_COUNT + 1] = {%s{0, 0, 0}};' % ''.join(
            '{I_%s, %d, %d}, ' % s for s in item_spawns))
        with open(os.path.join(outdir, 'quest_data.h'), 'w') as f:
            f.write('\n'.join(h) + '\n')
        with open(os.path.join(outdir, 'quest_data.c'), 'w') as f:
            f.write('\n'.join(c) + '\n')


def emit(outdir, font_w, world, npc_kinds):
    src = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'quests.txt')
    comp = Compiler(font_w, world)
    try:
        comp.parse(src)
        comp.link()
        comp.write(outdir, npc_kinds, world.ITEM_SPAWNS)
    except QuestError as e:
        raise SystemExit('quests.txt: %s' % e)
    import sys
    print('quests: %d quests, %d bytes of script, %d strings' % (len(comp.quests), len(comp.code), len(comp.strings)),
          file=sys.stderr)
