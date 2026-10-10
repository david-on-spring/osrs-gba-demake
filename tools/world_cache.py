"""On-disk cache of the world that tools/world_gen.py builds.

Importing world_gen lays out the map, renders every cell with hd_art and packs the tiles into
palettes, which is most of a gen_assets.py run. load() returns a snapshot of just the data the
emitters need (tools/world_emit.py, spawns.txt, gen_quests.py) and keeps it in
<outdir>/cache/world-<key>.pickle, where key hashes everything world_gen's result depends on:
world_gen.py, hd_art.py, tools/zones/*, this file, the WG_* environment knobs and the Python
version. Any change to those rebuilds the world; edits elsewhere (quests.txt, sprites, emission)
reuse it. WG_NOCACHE=1 skips the cache; MAPDUMP runs world_gen for real (it writes the map).
"""
import hashlib
import os
import pickle
import sys
from types import SimpleNamespace

TOOLS = os.path.dirname(os.path.abspath(__file__))

# world_gen globals in the snapshot; values must be plain data (see _plain)
FIELDS = ('MW', 'MH', 'OW_H', 'T', 'TY', 'ROOF', 'WALKSET', 'START', 'QPOS', 'SPAWNS', 'QUEST_OBJS',
          'ITEM_SPAWNS', 'FISH_SPOTS', 'SHOPS', 'REGIONS', 'PAL_REGIONS', 'NSETS', 'idx_tiles', 'meta_out',
          'map_meta', 'map_over', 'RES_ALT', 'world_pal', 'water_slots')
CTX_FIELDS = ('OX', 'OY', 'S', 'CX0', 'CY0', 'X0', 'Y0', 'X1', 'Y1')
ENV = ('WG_TRIALS', 'WG_SHARED')


def _inputs():
    files = [os.path.join(TOOLS, n) for n in ('world_gen.py', 'hd_art.py', 'world_cache.py')]
    zdir = os.path.join(TOOLS, 'zones')
    files += [os.path.join(zdir, n) for n in sorted(os.listdir(zdir)) if n.endswith(('.py', '.ref'))]
    return files


def key():
    h = hashlib.sha256()
    h.update(repr((sys.version_info[:2], [os.environ.get(e) for e in ENV])).encode())
    for p in _inputs():
        with open(p, 'rb') as f:
            data = f.read()
        h.update(b'%s\0%d\0' % (os.path.relpath(p, TOOLS).encode(), len(data)))
        h.update(data)
    return h.hexdigest()[:24]


def _ctx(z):
    return SimpleNamespace(**{k: getattr(z, k) for k in CTX_FIELDS})


def snapshot(wg):
    w = SimpleNamespace(**{k: getattr(wg, k) for k in FIELDS})
    w.ZONES = [(n, _ctx(z), ps, pl_) for (n, z, ps, pl_) in wg.ZONES]
    w.CITIES = [SimpleNamespace(name=c.name, ctx=_ctx(c.ctx)) for c in wg.CITIES]
    return w


class _Loader(pickle.Unpickler):
    """Only plain data: a snapshot must never pull world_gen (or anything else) back in."""

    def find_class(self, module, name):
        if (module, name) in (('types', 'SimpleNamespace'), ('builtins', 'set'), ('builtins', 'frozenset')):
            return super().find_class(module, name)
        raise pickle.UnpicklingError('world cache: unexpected %s.%s' % (module, name))


def load(outdir):
    if os.environ.get('MAPDUMP') or os.environ.get('WG_NOCACHE'):
        import world_gen
        return snapshot(world_gen)
    cdir = os.path.join(outdir, 'cache')
    k = key()
    path = os.path.join(cdir, 'world-%s.pickle' % k)
    try:
        with open(path, 'rb') as f:
            w = _Loader(f).load()
        print('world: cached (%s)' % os.path.basename(path), file=sys.stderr)
        return w
    except (OSError, EOFError, pickle.UnpicklingError, AttributeError, ValueError):
        pass
    import world_gen
    w = snapshot(world_gen)
    os.makedirs(cdir, exist_ok=True)
    tmp = path + '.tmp%d' % os.getpid()
    with open(tmp, 'wb') as f:
        pickle.dump(w, f, protocol=pickle.HIGHEST_PROTOCOL)
    os.replace(tmp, path)
    for n in os.listdir(cdir):          # keep only the current world
        if n.startswith('world-') and n.endswith('.pickle') and n != os.path.basename(path):
            os.remove(os.path.join(cdir, n))
    return w
