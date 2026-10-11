"""On-disk cache of the world that tools/world_gen.py builds.

Importing world_gen lays out the map, renders every cell with hd_art and packs the tiles into
palettes, which is most of a gen_assets.py run. load() returns a snapshot of just the data the
emitters need (tools/world_emit.py, spawns.txt, gen_quests.py) and keeps it in
<outdir>/cache/world-<key>.pickle, where key hashes everything world_gen's result depends on:
world_gen.py, hd_art.py, lz77.py, tools/zones/*, this file, the WG_* environment knobs and the Python
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
FIELDS = ('MW', 'MH', 'OW_W', 'OW_H', 'T', 'TY', 'WALKSET', 'START', 'QPOS', 'SPAWNS', 'QUEST_OBJS', 'FORGES',
          'ITEM_SPAWNS', 'FISH_SPOTS', 'SHOPS', 'REGIONS', 'PAL_REGIONS', 'NSETS', 'idx_tiles', 'meta_out',
          'RES_ALT', 'world_pal', 'water_slots', 'MAP_REGIONS', 'BOUNDS', 'NPLANES', 'WORLD_X0', 'WORLD_Y1', 'EDGES')
CTX_FIELDS = ('OX', 'OY', 'S', 'CX0', 'CY0', 'X0', 'Y0', 'X1', 'Y1')
ENV = ('WG_TRIALS', 'WG_SHARED')


def _inputs():
    files = [os.path.join(TOOLS, n) for n in ('world_gen.py', 'hd_art.py', 'lz77.py', 'world_cache.py')]
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


def _helpers(w):
    """world_gen's osrs_of / osrs_rect, over the snapshot's zones (not pickled)."""
    def osrs_of(x, y):
        """Grid cell -> absolute OSRS tile (X, Y, plane)."""
        for (n, z, ps, p) in w.ZONES:
            if z.X0 <= x <= z.X1 and z.Y0 <= y <= z.Y1:
                return (z.OX + (x - z.CX0) * z.S, z.OY - (y - z.CY0) * z.S, p)
        raise ValueError('cell %d,%d lies in no zone' % (x, y))

    def osrs_rect(x0, y0, x1, y1):
        """Cell rect -> OSRS (X0, Y0, X1, Y1, plane), with Y0 the south edge."""
        a, b = osrs_of(x0, y1), osrs_of(x1, y0)
        assert a[2] == b[2], ('rect spans planes', x0, y0, x1, y1)
        return (a[0], a[1], b[0], b[1], a[2])
    w.osrs_of, w.osrs_rect = osrs_of, osrs_rect
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
        return _helpers(snapshot(world_gen))
    cdir = os.path.join(outdir, 'cache')
    k = key()
    path = os.path.join(cdir, 'world-%s.pickle' % k)
    try:
        with open(path, 'rb') as f:
            w = _Loader(f).load()
        print('world: cached (%s)' % os.path.basename(path), file=sys.stderr)
        return _helpers(w)
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
    return _helpers(w)
