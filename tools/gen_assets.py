#!/usr/bin/env python3
"""INFINITE BEST asset generator.

Parses the human-editable ASCII-art sources in ``assets/`` and writes
``src/gb/assets.c`` + ``src/gb/assets.h`` (GBDK-2020 / SDCC friendly).

Stdlib only, fully deterministic (same input -> byte-identical output).

Source format (all files in assets/*.txt)
-----------------------------------------
* Lines starting with ``#`` (in column 0) are comments; blank lines end a block.
* A block starts with a header line ``@<kind> <NAME...> [key=value ...]``
  followed by rows of pixels.  Pixel characters:
      ``.`` = colour index 0 (background / transparent for sprites)
      ``1`` = index 1 (dim)   ``2`` = index 2 (bright)   ``3`` = index 3 (hot)
  Block width/height are taken from the rows and must be multiples of 8.
  Multi-tile blocks are split into 8x8 tiles in row-major order, so a 16x16
  block becomes TL, TR, BL, BR.
* Kinds:
    ``@char X``       font glyph for character X (``@char SPACE`` for ' ').
    ``@tile NAME``    UI tile / sprite / fade tile.  A multi-tile block may
                      name every tile (``@tile UI_INFINITY_L UI_INFINITY_R``)
                      or just the first one (``@tile SPR_PLAYER_IDLE``).
    ``@meta NAME pal=PALNAME``  16x16 metatile with its CGB BG palette.
    ``@logo``         the title logo pixel canvas (width = LOGO_W*8).
    ``@logo_attr``    one char per logo cell: CGB BG palette number 0-7.
    ``@bgpal N NAME #rrggbb x4`` / ``@sprpal N NAME #rrggbb x4`` palettes
                      (single line, no pixel rows).
"""

import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

TILE_FONT_BASE = 0
TILE_UI_BASE = 64
TILE_MT_BASE = 80
TILE_SPR_BASE = 160
TILE_FADE_BASE = 192
NUM_FONT = 64
NUM_UI = 16
NUM_SPR = 32
NUM_FADE = 8
MAX_MT = 20
MAX_LOGO_TILES = 80

PIX = {'.': 0, '1': 1, '2': 2, '3': 3}

MT_ORDER = [
    'MT_FLOOR', 'MT_WALL', 'MT_WALL2', 'MT_EXIT_LOCKED', 'MT_EXIT_OPEN',
    'MT_EXIT_OPEN2', 'MT_CHIP', 'MT_STOP', 'MT_ARROW_U', 'MT_ARROW_R',
    'MT_ARROW_D', 'MT_ARROW_L', 'MT_SWITCH_0', 'MT_SWITCH_1',
    'MT_GATE_A_CLOSED', 'MT_GATE_A_OPEN', 'MT_GATE_B_CLOSED',
    'MT_GATE_B_OPEN', 'MT_PORTAL', 'MT_PIT',
]

UI_REQUIRED = [
    'UI_BOLT', 'UI_STEP', 'UI_STAR', 'UI_CHIP', 'UI_FLAG',
    'UI_BOX_TL', 'UI_BOX_T', 'UI_BOX_TR', 'UI_BOX_L', 'UI_BOX_R',
    'UI_BOX_BL', 'UI_BOX_B', 'UI_BOX_BR', 'UI_BAR_FULL', 'UI_BAR_HALF',
    'UI_INFINITY_L', 'UI_INFINITY_R',
]

SPR_REQUIRED = [
    'SPR_PLAYER_IDLE', 'SPR_PLAYER_BLINK', 'SPR_PLAYER_SQUASH',
    'SPR_PLAYER_DEAD', 'SPR_SPARK_BIG', 'SPR_SPARK_SMALL', 'SPR_PIXEL',
    'SPR_TRAIL', 'SPR_HINT_UP', 'SPR_HINT_RIGHT', 'SPR_CURSOR', 'SPR_STAR',
    'SPR_RING_A', 'SPR_RING_B',
]


class AssetError(Exception):
    pass


# --------------------------------------------------------------------------
# Parsing
# --------------------------------------------------------------------------

class Block(object):
    def __init__(self, kind, args, opts, rows, where):
        self.kind = kind
        self.args = args
        self.opts = opts
        self.rows = rows
        self.where = where

    @property
    def w(self):
        return len(self.rows[0]) if self.rows else 0

    @property
    def h(self):
        return len(self.rows)


def parse_file(path):
    blocks = []
    cur = None
    with open(path, 'r') as f:
        lines = f.read().split('\n')
    for ln, raw in enumerate(lines, 1):
        line = raw.rstrip()
        where = '%s:%d' % (os.path.basename(path), ln)
        if line.startswith('#'):
            continue
        if not line.strip():
            cur = None
            continue
        if line.startswith('@'):
            toks = line[1:].split()
            kind = toks[0]
            args, opts = [], {}
            for t in toks[1:]:
                if kind == 'char':
                    args.append(t)  # '@char =' is a glyph, not an option
                elif '=' in t and not t.startswith('#'):
                    k, v = t.split('=', 1)
                    opts[k] = v
                else:
                    args.append(t)
            cur = Block(kind, args, opts, [], where)
            blocks.append(cur)
            continue
        if cur is None:
            raise AssetError('%s: pixel row outside of a block' % where)
        row = line.strip()
        for ch in row:
            if cur.kind != 'logo_attr' and ch not in PIX:
                raise AssetError('%s: bad pixel char %r' % (where, ch))
        if cur.rows and len(row) != len(cur.rows[0]):
            raise AssetError('%s: row width %d != %d' %
                             (where, len(row), len(cur.rows[0])))
        cur.rows.append(row)
    return blocks


def split_tiles(block):
    """Split a block into 8x8 tiles (row-major). Each tile = list of 8 rows
    of 8 ints."""
    if block.kind != 'logo_attr':
        if block.w % 8 or block.h % 8 or not block.rows:
            raise AssetError('%s: block size %dx%d not a multiple of 8' %
                             (block.where, block.w, block.h))
    tiles = []
    for ty in range(block.h // 8):
        for tx in range(block.w // 8):
            t = []
            for y in range(8):
                r = block.rows[ty * 8 + y][tx * 8:tx * 8 + 8]
                t.append([PIX[c] for c in r])
            tiles.append(t)
    return tiles


def encode_tile(tile):
    out = []
    for row in tile:
        lo = hi = 0
        for x, v in enumerate(row):
            bit = 7 - x
            lo |= (v & 1) << bit
            hi |= ((v >> 1) & 1) << bit
        out.append(lo)
        out.append(hi)
    return out


BLANK = [[0] * 8 for _ in range(8)]


def parse_color(s, where):
    s = s.lstrip('#')
    if len(s) != 6:
        raise AssetError('%s: bad colour %r' % (where, s))
    r, g, b = int(s[0:2], 16), int(s[2:4], 16), int(s[4:6], 16)
    return (r, g, b)


def rgb555(c):
    r, g, b = c
    return (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10)


def load_assets(assets_dir, strict=True):
    """Parse every assets/*.txt file and return a dict with everything the
    C writer and the previewer need."""
    blocks = []
    for name in sorted(os.listdir(assets_dir)):
        if name.endswith('.txt'):
            blocks.extend(parse_file(os.path.join(assets_dir, name)))

    font = {}
    ui = []          # list of (name, tile)
    metas = {}       # name -> (tiles[4], palname)
    spr = []         # list of (name or None, tile)
    fade = []
    logo_block = None
    attr_block = None
    bgpal = {}
    sprpal = {}

    for b in blocks:
        k = b.kind
        if k in ('bgpal', 'sprpal'):
            if len(b.args) != 6:
                raise AssetError('%s: palette needs N NAME c0 c1 c2 c3' %
                                 b.where)
            n = int(b.args[0])
            cols = [parse_color(c, b.where) for c in b.args[2:]]
            dest = bgpal if k == 'bgpal' else sprpal
            if n in dest or not 0 <= n <= 7:
                raise AssetError('%s: bad/duplicate palette %d' % (b.where, n))
            dest[n] = (b.args[1], cols)
        elif k == 'char':
            tok = b.args[0]
            ch = ' ' if tok == 'SPACE' else tok
            if len(ch) != 1 or not 32 <= ord(ch) <= 95:
                raise AssetError('%s: bad char %r' % (b.where, tok))
            if ch in font:
                raise AssetError('%s: duplicate char %r' % (b.where, ch))
            t = split_tiles(b)
            if len(t) != 1:
                raise AssetError('%s: glyph must be 8x8' % b.where)
            font[ch] = t[0]
        elif k == 'tile':
            section = b.opts.get('section')
            tiles = split_tiles(b)
            names = list(b.args)
            if len(names) == 1:
                names = names + [None] * (len(tiles) - 1)
            if len(names) != len(tiles):
                raise AssetError('%s: %d names for %d tiles' %
                                 (b.where, len(names), len(tiles)))
            first = names[0]
            if section is None:
                if first.startswith('UI_'):
                    section = 'ui'
                elif first.startswith('SPR_'):
                    section = 'spr'
                elif first.startswith('FADE_'):
                    section = 'fade'
                else:
                    raise AssetError('%s: unknown tile section for %s' %
                                     (b.where, first))
            dest = {'ui': ui, 'spr': spr, 'fade': fade}[section]
            dest.extend(zip(names, tiles))
        elif k == 'meta':
            name = b.args[0]
            if name in metas:
                raise AssetError('%s: duplicate %s' % (b.where, name))
            tiles = split_tiles(b)
            if len(tiles) != 4:
                raise AssetError('%s: metatile must be 16x16' % b.where)
            pal = b.opts.get('pal')
            if pal is None:
                raise AssetError('%s: metatile needs pal=' % b.where)
            metas[name] = (tiles, pal)
        elif k == 'logo':
            logo_block = b
        elif k == 'logo_attr':
            attr_block = b
        else:
            raise AssetError('%s: unknown block kind @%s' % (b.where, k))

    # ---- validate / order -------------------------------------------------
    missing = [chr(c) for c in range(32, 96) if chr(c) not in font]
    if missing:
        raise AssetError('font missing glyphs: %r' % ''.join(missing))
    font_tiles = [font[chr(c)] for c in range(32, 96)]
    if any(any(r) for r in font_tiles[0]):
        raise AssetError('space glyph must be blank')

    ui_names = [a for n, _ in ui if n for a in n.split('|')]
    for n in UI_REQUIRED:
        if n not in ui_names:
            raise AssetError('missing UI tile %s' % n)
    if len(ui) > NUM_UI:
        raise AssetError('too many UI tiles (%d > %d)' % (len(ui), NUM_UI))
    i = 0
    while len(ui) < NUM_UI:
        ui.append(('UI_FREE_%d' % i, BLANK))
        i += 1

    for n in metas:
        if n not in MT_ORDER:
            raise AssetError('unknown metatile %s' % n)
    for n in MT_ORDER:
        if n not in metas:
            raise AssetError('missing metatile %s' % n)

    spr_names = [a for n, _ in spr if n for a in n.split('|')]
    for n in SPR_REQUIRED:
        if n not in spr_names:
            raise AssetError('missing sprite %s' % n)
    if len(spr) > NUM_SPR:
        raise AssetError('too many sprite tiles (%d)' % len(spr))
    while len(spr) < NUM_SPR:
        spr.append((None, BLANK))

    if len(fade) != NUM_FADE:
        raise AssetError('need exactly %d fade tiles, got %d' %
                         (NUM_FADE, len(fade)))
    if any(v != 0 for r in fade[0][1] for v in r):
        raise AssetError('fade tile 0 must be all index 0')
    if any(v != 3 for r in fade[-1][1] for v in r):
        raise AssetError('last fade tile must be all index 3')

    for n in range(8):
        if n not in bgpal:
            raise AssetError('missing bg palette %d' % n)
        if n not in sprpal:
            raise AssetError('missing sprite palette %d' % n)
    bg0 = bgpal[0][1][0]
    for n in range(8):
        if bgpal[n][1][0] != bg0:
            raise AssetError('bg palette %d colour 0 differs from palette 0'
                             % n)
    palnum = dict((bgpal[n][0], n) for n in bgpal)

    mt_list = []
    for n in MT_ORDER:
        tiles, pal = metas[n]
        if pal not in palnum:
            raise AssetError('%s: unknown palette %s' % (n, pal))
        mt_list.append((n, tiles, palnum[pal]))

    # ---- logo -------------------------------------------------------------
    if logo_block is None or attr_block is None:
        raise AssetError('missing @logo / @logo_attr')
    logo_w = logo_block.w // 8
    logo_h = logo_block.h // 8
    if logo_w > 20 or logo_h > 18:
        raise AssetError('logo too big (%dx%d tiles)' % (logo_w, logo_h))
    cells = split_tiles(logo_block)
    fixed = {}  # encoded -> absolute index for font+ui tiles reusable as-is
    for i, t in enumerate(font_tiles):
        fixed.setdefault(tuple(encode_tile(t)), TILE_FONT_BASE + i)
    for i, (_, t) in enumerate(ui):
        fixed.setdefault(tuple(encode_tile(t)), TILE_UI_BASE + i)
    uniq = []
    seen = {}
    logo_map = []
    for t in cells:
        e = tuple(encode_tile(t))
        if e in fixed:
            logo_map.append(fixed[e])
            continue
        if e not in seen:
            seen[e] = len(uniq)
            uniq.append(t)
        logo_map.append(TILE_MT_BASE + seen[e])
    if strict and len(uniq) > MAX_LOGO_TILES:
        raise AssetError('logo uses %d unique tiles (> %d)' %
                         (len(uniq), MAX_LOGO_TILES))
    if attr_block.h != logo_h or attr_block.w != logo_w:
        raise AssetError('logo_attr must be %dx%d chars' % (logo_w, logo_h))
    logo_attr = []
    for row in attr_block.rows:
        for ch in row:
            if ch not in '01234567':
                raise AssetError('logo_attr: bad palette %r' % ch)
            logo_attr.append(int(ch))

    return {
        'font': font_tiles,
        'ui': ui,
        'mt': mt_list,
        'spr': spr,
        'fade': fade,
        'logo_w': logo_w,
        'logo_h': logo_h,
        'logo_tiles': uniq,
        'logo_cells': cells,
        'logo_map': logo_map,
        'logo_attr': logo_attr,
        'bgpal': [bgpal[n] for n in range(8)],
        'sprpal': [sprpal[n] for n in range(8)],
    }


# --------------------------------------------------------------------------
# C output
# --------------------------------------------------------------------------

def c_bytes(data, indent='    ', per_line=16):
    lines = []
    for i in range(0, len(data), per_line):
        lines.append(indent + ','.join('0x%02X' % b for b in data[i:i + per_line]) + ',')
    return '\n'.join(lines)


def tiles_bytes(tiles):
    out = []
    for t in tiles:
        out.extend(encode_tile(t))
    return out


def tile_array(name, count_expr, tiles, labels=None):
    s = ['const uint8_t %s[%s] = {' % (name, count_expr)]
    for i, t in enumerate(tiles):
        if labels and labels[i]:
            s.append('    /* %s */' % labels[i])
        s.append(c_bytes(encode_tile(t)))
    s.append('};')
    return '\n'.join(s)


def enum_block(items, comment=None):
    s = []
    if comment:
        s.append('/* %s */' % comment)
    s.append('enum {')
    for i, (n, v) in enumerate(items):
        sep = ',' if i < len(items) - 1 else ''
        if v is None:
            s.append('    %s%s' % (n, sep))
        else:
            s.append('    %s = %s%s' % (n, v, sep))
    s.append('};')
    return '\n'.join(s)


def font_label(i):
    ch = chr(32 + i)
    if ch == ' ':
        return "' '"
    if ch == '\\':
        return 'backslash'
    if ch in '*/':
        return "'%s'" % ch
    return "'%s'" % ch


def gen(data):
    H = []
    H.append('/* AUTO-GENERATED by tools/gen_assets.py from the assets/ directory -- DO NOT EDIT */')
    H.append('#ifndef INFINITE_BEST_ASSETS_H')
    H.append('#define INFINITE_BEST_ASSETS_H')
    H.append('')
    H.append('#include <stdint.h>')
    H.append('')
    H.append('/* ---- tile index layout (shared 256-tile space at 0x8000) ---- */')
    H.append('#define TILE_FONT_BASE %d' % TILE_FONT_BASE)
    H.append('#define TILE_UI_BASE %d' % TILE_UI_BASE)
    H.append('#define TILE_MT_BASE %d' % TILE_MT_BASE)
    H.append('#define TILE_SPR_BASE %d' % TILE_SPR_BASE)
    H.append('#define TILE_FADE_BASE %d' % TILE_FADE_BASE)
    H.append('#define NUM_FONT_TILES %d' % NUM_FONT)
    H.append('#define NUM_UI_TILES %d' % NUM_UI)
    H.append('#define NUM_SPR_TILES %d' % NUM_SPR)
    H.append('#define NUM_FADE_TILES %d' % NUM_FADE)
    H.append('#define TILE_BLANK 0                           /* space: all index 0 */')
    H.append('#define TILE_SOLID3 (TILE_FADE_BASE + NUM_FADE_TILES - 1) /* all index 3 */')
    H.append('#define FONT_TILE(ch) ((uint8_t)((ch) - 32 + TILE_FONT_BASE)) /* ASCII 32..95 */')
    H.append('#define MT_TILE(m, corner) ((uint8_t)(TILE_MT_BASE + ((m) << 2) + (corner))) /* corner 0=TL 1=TR 2=BL 3=BR */')
    H.append('')
    ui_items = []
    for i, (n, _) in enumerate(data['ui']):
        for a in n.split('|'):
            ui_items.append((a, 'TILE_UI_BASE + %d' % i))
    H.append(enum_block(ui_items, 'UI tiles: absolute tile indices.  The box edges are symmetric, so\n   UI_BOX_B == UI_BOX_T and UI_BOX_R == UI_BOX_L; box interior = TILE_BLANK'))
    H.append('')
    mt_items = [(n, None) for n, _, _ in data['mt']] + [('NUM_MT', None)]
    mt_items[0] = (mt_items[0][0], '0')
    H.append(enum_block(mt_items, 'metatile ids; tile of metatile m corner c = TILE_MT_BASE + m*4 + c'))
    H.append('')
    spr_items = []
    used = max(i for i, (n, _) in enumerate(data['spr']) if n) + 1
    while used < len(data['spr']) and any(v for r in data['spr'][used][1] for v in r):
        used += 1
    for i, (n, _) in enumerate(data['spr']):
        if n:
            for a in n.split('|'):
                spr_items.append((a, 'TILE_SPR_BASE + %d' % i))
    H.append(enum_block(spr_items, 'sprite tiles: absolute indices; 16x16 player frames are TL,TR,BL,BR.\n   SPR_HINT_UP/RIGHT: flip for down/left.  SPR_RING_B is the top-left\n   quarter of a 16x16 ring: draw 4 with X/Y flips'))
    H.append('#define SPR_FIRST_FREE (TILE_SPR_BASE + %d)  /* unused (blank) slots up to 191 */' % used)
    H.append('')
    H.append('extern const uint8_t font_tiles[64*16];')
    H.append('extern const uint8_t ui_tiles[16*16];')
    H.append('extern const uint8_t mt_tiles[NUM_MT*4*16];')
    H.append('extern const uint8_t spr_tiles[32*16];')
    H.append('extern const uint8_t fade_tiles[NUM_FADE_TILES*16];')
    H.append('')
    H.append('/* ---- title logo (tiles load at TILE_MT_BASE; map holds ABSOLUTE indices) ---- */')
    H.append('#define LOGO_W %d' % data['logo_w'])
    H.append('#define LOGO_H %d' % data['logo_h'])
    H.append('#define LOGO_NTILES %d' % len(data['logo_tiles']))
    H.append('extern const uint8_t logo_tiles[LOGO_NTILES*16];')
    H.append('extern const uint8_t logo_map[LOGO_W*LOGO_H];')
    H.append('')
    H.append('/* ---- CGB palettes ---- */')
    H.append('/* BG palette numbers */')
    for n, (name, cols) in enumerate(data['bgpal']):
        H.append('#define %s %d' % (name, n))
    H.append('#define PAL_EXIT PAL_UI           /* exit port (cyan) */')
    H.append('#define PAL_ARROW PAL_AMBER       /* conveyors (yellow) */')
    H.append('#define PAL_GATE_B PAL_AMBER      /* gate B (amber) */')
    H.append('#define PAL_SWITCH PAL_CHIP       /* toggle switch (green) */')
    H.append('#define PAL_HUD_ACCENT PAL_AMBER  /* HUD icons: bolt/step/star/flag/bar/infinity */')
    H.append('#define PAL_HUD_CHIP PAL_CHIP     /* HUD chip counter icon */')
    H.append('#define PAL_LOGO PAL_UI           /* logo "INFINITE" */')
    H.append('#define PAL_LOGO2 PAL_GATE_A      /* logo "BEST" */')
    H.append('#define PAL_LOGO3 PAL_AMBER       /* logo infinity divider */')
    H.append('/* sprite palette numbers */')
    for n, (name, cols) in enumerate(data['sprpal']):
        H.append('#define %s %d' % (name, n))
    H.append('/* DMG palettes: inverted so index 0 = black, 3 = white */')
    H.append('#define DMG_BGP 0x1B')
    H.append('#define DMG_OBP0 0x1B')
    H.append('#define DMG_OBP1 0xC4   /* live player: bright face, dark eyes, off the grey walls */')
    H.append('extern const uint8_t mt_cgb_pal[NUM_MT];     /* BG palette per metatile */')
    H.append('extern const uint8_t logo_cgb_attr[LOGO_W*LOGO_H]; /* BG palette per logo cell */')
    H.append('extern const uint16_t bg_cgb_pal[8*4];       /* RGB555: r | g<<5 | b<<10 */')
    H.append('extern const uint16_t spr_cgb_pal[8*4];')
    H.append('')
    H.append('#endif')
    H.append('')

    C = []
    C.append('/* AUTO-GENERATED by tools/gen_assets.py from the assets/ directory -- DO NOT EDIT */')
    C.append('/* On the Game Boy all art lives in a switchable ROM bank; gfx.c maps it in to load it. */')
    C.append('#ifdef __SDCC')
    C.append('#pragma bank 255')
    C.append('#include <gb/gb.h>')
    C.append('BANKREF(assets)')
    C.append('#endif')
    C.append('#include "assets.h"')
    C.append('')
    C.append(tile_array('font_tiles', '64*16', data['font'],
                        [font_label(i) for i in range(64)]))
    C.append('')
    C.append(tile_array('ui_tiles', '16*16', [t for _, t in data['ui']],
                        [n for n, _ in data['ui']]))
    C.append('')
    mt_tiles, mt_labels = [], []
    for n, tiles, _ in data['mt']:
        mt_tiles.extend(tiles)
        mt_labels.extend([n + ' TL', 'TR', 'BL', 'BR'])
    C.append(tile_array('mt_tiles', 'NUM_MT*4*16', mt_tiles, mt_labels))
    C.append('')
    C.append(tile_array('spr_tiles', '32*16', [t for _, t in data['spr']],
                        [n for n, _ in data['spr']]))
    C.append('')
    C.append(tile_array('fade_tiles', 'NUM_FADE_TILES*16',
                        [t for _, t in data['fade']],
                        [n for n, _ in data['fade']]))
    C.append('')
    C.append(tile_array('logo_tiles', 'LOGO_NTILES*16', data['logo_tiles']))
    C.append('')
    lw = data['logo_w']
    C.append('const uint8_t logo_map[LOGO_W*LOGO_H] = {')
    for r in range(data['logo_h']):
        C.append('    ' + ','.join('%3d' % v for v in data['logo_map'][r * lw:(r + 1) * lw]) + ',')
    C.append('};')
    C.append('')
    C.append('const uint8_t logo_cgb_attr[LOGO_W*LOGO_H] = {')
    for r in range(data['logo_h']):
        C.append('    ' + ','.join('%d' % v for v in data['logo_attr'][r * lw:(r + 1) * lw]) + ',')
    C.append('};')
    C.append('')
    C.append('const uint8_t mt_cgb_pal[NUM_MT] = {')
    for n, _, p in data['mt']:
        C.append('    %d, /* %s */' % (p, n))
    C.append('};')
    C.append('')
    for arr, pals in (('bg_cgb_pal', data['bgpal']), ('spr_cgb_pal', data['sprpal'])):
        C.append('const uint16_t %s[8*4] = {' % arr)
        for name, cols in pals:
            C.append('    ' + ','.join('0x%04X' % rgb555(c) for c in cols) +
                     ', /* %s */' % name)
        C.append('};')
        C.append('')
    return '\n'.join(H), '\n'.join(C)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--assets', default=os.path.join(ROOT, 'assets'))
    ap.add_argument('--out-dir', default=os.path.join(ROOT, 'src', 'gb'))
    args = ap.parse_args(argv)
    try:
        data = load_assets(args.assets)
    except AssetError as e:
        sys.stderr.write('gen_assets: error: %s\n' % e)
        return 1
    h, c = gen(data)
    if not os.path.isdir(args.out_dir):
        os.makedirs(args.out_dir)
    for fn, txt in (('assets.h', h), ('assets.c', c)):
        p = os.path.join(args.out_dir, fn)
        old = None
        if os.path.exists(p):
            with open(p, 'r') as f:
                old = f.read()
        if old != txt:
            with open(p, 'w') as f:
                f.write(txt)
    print('gen_assets: %d metatiles, logo %dx%d (%d unique tiles) -> %s' %
          (len(data['mt']), data['logo_w'], data['logo_h'],
           len(data['logo_tiles']), args.out_dir))
    return 0


if __name__ == '__main__':
    sys.exit(main())
