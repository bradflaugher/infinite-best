#!/usr/bin/env python3
"""Render a preview sheet of every INFINITE BEST asset to build/asset_preview.png.

Shows (4x scale): the title screen and a mock level screen at 160x144 in both
DMG (BGP=0x1B, inverted grey) and CGB colours, then the font, UI tiles,
metatiles, sprites and fade tiles on their CGB palettes.

Requires Pillow.  Usage: python3 tools/preview_assets.py [--out PATH] [--scale N]
"""

import argparse
import os
import sys

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen_assets as G  # noqa: E402

ROOT = G.ROOT
DMG_SHADES = [(232, 236, 232), (160, 168, 164), (84, 92, 96), (12, 14, 20)]
BGP = 0x1B
SHEET_BG = (22, 24, 34)
LABEL = (150, 160, 190)


def c555_to_rgb(v):
    r, g, b = v & 31, (v >> 5) & 31, (v >> 10) & 31
    return tuple((c << 3) | (c >> 2) for c in (r, g, b))


def cgb_pal(pal):
    return [c555_to_rgb(G.rgb555(c)) for c in pal[1]]


def dmg_pal(reg=BGP):
    return [DMG_SHADES[(reg >> (2 * i)) & 3] for i in range(4)]


def draw_tile(img, tile, x, y, pal, transparent=False):
    for ty in range(8):
        for tx in range(8):
            v = tile[ty][tx]
            if transparent and v == 0:
                continue
            img.putpixel((x + tx, y + ty), pal[v])


class Screen(object):
    """A 20x18 BG map + sprites, rendered at native res."""

    def __init__(self, data):
        self.d = data
        self.vram = {}
        for i, t in enumerate(data['font']):
            self.vram[G.TILE_FONT_BASE + i] = t
        for i, (_, t) in enumerate(data['ui']):
            self.vram[G.TILE_UI_BASE + i] = t
        for m, (_, tiles, _) in enumerate(data['mt']):
            for c in range(4):
                self.vram[G.TILE_MT_BASE + m * 4 + c] = tiles[c]
        for i, (_, t) in enumerate(data['spr']):
            self.vram[G.TILE_SPR_BASE + i] = t
        for i, (_, t) in enumerate(data['fade']):
            self.vram[G.TILE_FADE_BASE + i] = t
        self.map = [[0] * 20 for _ in range(18)]
        self.attr = [[0] * 20 for _ in range(18)]
        self.sprites = []  # (x, y, tile, spal, flipx, flipy)

    def load_logo(self):
        for i, t in enumerate(self.d['logo_tiles']):
            self.vram[G.TILE_MT_BASE + i] = t

    def text(self, x, y, s, pal=0):
        for i, ch in enumerate(s):
            self.map[y][x + i] = ord(ch) - 32
            self.attr[y][x + i] = pal

    def put(self, x, y, tile, pal=0):
        self.map[y][x] = tile
        self.attr[y][x] = pal

    def meta(self, cx, cy, m, y0=2):
        pal = self.d['mt'][m][2]
        for c in range(4):
            self.put(cx * 2 + (c & 1), y0 + cy * 2 + (c >> 1),
                     G.TILE_MT_BASE + m * 4 + c, pal)

    def spr16(self, x, y, base, spal):
        for c in range(4):
            self.sprites.append((x + (c & 1) * 8, y + (c >> 1) * 8, base + c, spal, False, False))

    def render(self, cgb):
        img = Image.new('RGB', (160, 144))
        bgp = [cgb_pal(p) for p in self.d['bgpal']] if cgb else None
        obp = [cgb_pal(p) for p in self.d['sprpal']] if cgb else None
        for ty in range(18):
            for tx in range(20):
                pal = bgp[self.attr[ty][tx]] if cgb else dmg_pal()
                draw_tile(img, self.vram[self.map[ty][tx]], tx * 8, ty * 8, pal)
        for (x, y, t, sp, fx, fy) in self.sprites:
            tile = self.vram[t]
            if fx:
                tile = [list(reversed(r)) for r in tile]
            if fy:
                tile = list(reversed(tile))
            pal = obp[sp] if cgb else dmg_pal(0x1B)
            for yy in range(8):
                for xx in range(8):
                    v = tile[yy][xx]
                    if v and 0 <= x + xx < 160 and 0 <= y + yy < 144:
                        img.putpixel((x + xx, y + yy), pal[v])
        return img


MT = dict((n, i) for i, n in enumerate(G.MT_ORDER))

# 10x8 mock level; legend chars -> metatile
LEVEL = [
    'WWWWWWWWWW',
    'W..c...>.W',
    'W.w..A...W',
    'W.s.c.Bw.W',
    'Wx...^.o.W',
    'W.p..S.<.W',
    'W..w...E.W',
    'WWWWVWWWWW',
]
LEGEND = {'W': 'MT_WALL', 'w': 'MT_WALL2', '.': 'MT_FLOOR', 'c': 'MT_CHIP',
          '>': 'MT_ARROW_R', '<': 'MT_ARROW_L', '^': 'MT_ARROW_U',
          'v': 'MT_ARROW_D', 'A': 'MT_GATE_A_CLOSED', 'a': 'MT_GATE_A_OPEN',
          'B': 'MT_GATE_B_OPEN', 'b': 'MT_GATE_B_CLOSED', 's': 'MT_STOP',
          'x': 'MT_PIT', 'o': 'MT_PORTAL', 'S': 'MT_SWITCH_1',
          'E': 'MT_EXIT_OPEN', 'p': 'MT_FLOOR', 'V': 'MT_GATE_B_CLOSED'}


def level_screen(data):
    s = Screen(data)
    ui = dict((a, G.TILE_UI_BASE + i) for i, (n, _) in enumerate(data['ui'])
              for a in n.split('|'))
    accent = [p[0] for p in data['bgpal']].index('PAL_AMBER')
    chip_pal = [p[0] for p in data['bgpal']].index('PAL_CHIP')
    s.text(0, 0, 'LV042', 0)
    s.put(6, 0, ui['UI_STEP'], accent)
    s.text(7, 0, '07', 0)
    s.put(10, 0, ui['UI_FLAG'], accent)
    s.text(11, 0, '09', 0)
    s.put(14, 0, ui['UI_CHIP'], chip_pal)
    s.text(15, 0, '2/3', 0)
    s.put(18, 0, ui['UI_INFINITY_L'], accent)
    s.put(19, 0, ui['UI_INFINITY_R'], accent)
    s.put(0, 1, ui['UI_BOLT'], accent)
    for i in range(6):
        s.put(1 + i, 1, ui['UI_BAR_FULL'], accent)
    s.put(7, 1, ui['UI_BAR_HALF'], accent)
    s.put(8, 1, ui['UI_BAR_EMPTY'] if 'UI_BAR_EMPTY' in ui else 0, accent)
    s.text(10, 1, 'BEST', 0)
    s.put(15, 1, ui['UI_STAR'], accent)
    s.put(16, 1, ui['UI_STAR'], accent)
    s.put(17, 1, ui['UI_STAR'], accent)
    for cy, row in enumerate(LEVEL):
        for cx, ch in enumerate(row):
            s.meta(cx, cy, MT[LEGEND[ch]])
    spr = dict((n, G.TILE_SPR_BASE + i) for i, (n, _) in enumerate(data['spr']) if n)
    # player on 'p' cell, sliding right with a trail
    px, py = 2 * 16, 2 * 8 + 5 * 16
    s.spr16(px, py, spr['SPR_PLAYER_IDLE'], 0)
    s.sprites.append((px - 10, py + 4, spr['SPR_TRAIL'], 4, False, False))
    s.sprites.append((px - 18, py + 4, spr['SPR_PIXEL'], 4, False, False))
    s.sprites.append((7 * 16 + 4, 16 + 6 * 16 - 10, spr['SPR_SPARK_BIG'], 1, False, False))
    s.sprites.append((3 * 16 + 12, 16 + 1 * 16 + 2, spr['SPR_SPARK_SMALL'], 1, False, False))
    s.sprites.append((px + 4, py - 10, spr['SPR_HINT_UP'], 2, False, False))
    s.sprites.append((px + 18, py + 4, spr['SPR_HINT_RIGHT'], 2, False, False))
    s.sprites.append((4 * 16, 16 + 3 * 16 + 4, spr['SPR_STAR'], 3, False, False))
    return s


def title_screen(data):
    s = Screen(data)
    s.load_logo()
    lw, lh = data['logo_w'], data['logo_h']
    ox, oy = (20 - lw) // 2, 1
    for y in range(lh):
        for x in range(lw):
            s.put(ox + x, oy + y, data['logo_map'][y * lw + x],
                  data['logo_attr'][y * lw + x])
    ui = dict((a, G.TILE_UI_BASE + i) for i, (n, _) in enumerate(data['ui'])
              for a in n.split('|'))
    # menu box, rows 10..15, cols 1..18
    x0, y0, x1, y1 = 1, oy + lh + 1, 18, oy + lh + 6
    for x in range(x0 + 1, x1):
        s.put(x, y0, ui['UI_BOX_T'])
        s.put(x, y1, ui['UI_BOX_B'])
    for y in range(y0 + 1, y1):
        s.put(x0, y, ui['UI_BOX_L'])
        s.put(x1, y, ui['UI_BOX_R'])
    s.put(x0, y0, ui['UI_BOX_TL'])
    s.put(x1, y0, ui['UI_BOX_TR'])
    s.put(x0, y1, ui['UI_BOX_BL'])
    s.put(x1, y1, ui['UI_BOX_BR'])
    y = y0 + 1
    s.text(4, y, 'PRESS START', 0)
    spr = dict((n, G.TILE_SPR_BASE + i) for i, (n, _) in enumerate(data['spr']) if n)
    s.sprites.append((3 * 8 - 2, y * 8, spr['SPR_CURSOR'], 6, False, False))
    s.text(4, y + 1, 'SEED 00C0FFEE', 0)
    s.text(4, y + 2, 'PAR 09 BEST--', 0)
    s.put(4, y + 3, ui['UI_STAR'], 4)
    s.text(6, y + 3, 'X12', 0)
    s.put(10, y + 3, ui['UI_CHIP'], 2)
    s.text(12, y + 3, 'X34', 0)
    s.text(1, 17, '(C)2026 V1.0', 0)
    s.spr16(140, 124, spr['SPR_PLAYER_IDLE'], 0)
    s.sprites.append((128, 124, spr['SPR_SPARK_SMALL'], 1, False, False))
    return s


def sheet(data, scale):
    items = []  # (label, image)

    def tiles_row(tiles, pals, per_row, transparent=False, cols_gap=1):
        n = len(tiles)
        rows = (n + per_row - 1) // per_row
        img = Image.new('RGB', (per_row * (8 + cols_gap), rows * (8 + cols_gap)), SHEET_BG)
        for i, t in enumerate(tiles):
            x, y = (i % per_row) * (8 + cols_gap), (i // per_row) * (8 + cols_gap)
            if transparent:
                for yy in range(8):
                    for xx in range(8):
                        img.putpixel((x + xx, y + yy), (0, 0, 0))
            draw_tile(img, t, x, y, pals[i] if isinstance(pals, list) and isinstance(pals[0], list) else pals, transparent)
        return img

    bg = [cgb_pal(p) for p in data['bgpal']]
    sp = [cgb_pal(p) for p in data['sprpal']]
    items.append(('FONT (PAL_UI)', tiles_row(data['font'], bg[0], 16)))
    items.append(('FONT (DMG)', tiles_row(data['font'], dmg_pal(), 16)))
    items.append(('UI 64..79', tiles_row([t for _, t in data['ui']], bg[4], 16)))
    # metatiles
    n = len(data['mt'])
    img = Image.new('RGB', (10 * 18, ((n + 9) // 10) * 18 * 2), SHEET_BG)
    for m, (_, tiles, p) in enumerate(data['mt']):
        for mode in range(2):
            x = (m % 10) * 18
            y = (m // 10) * 36 + mode * 18
            pal = bg[p] if mode == 0 else dmg_pal()
            for c in range(4):
                draw_tile(img, tiles[c], x + (c & 1) * 8, y + (c >> 1) * 8, pal)
    items.append(('METATILES ' + ' '.join(n[3:] for n, _, _ in data['mt']), img))
    # sprites
    spr = data['spr']
    img = Image.new('RGB', (4 * 18 + 16 * 9, 2 * 18), SHEET_BG)
    for f in range(4):
        for mode in range(2):
            pal = sp[5 if f == 3 else 0] if mode == 0 else dmg_pal()
            for c in range(4):
                x = f * 18 + (c & 1) * 8
                y = mode * 18 + (c >> 1) * 8
                draw_tile(img, spr[f * 4 + c][1], x, y, pal)
    spal = {'SPR_SPARK_BIG': 1, 'SPR_SPARK_SMALL': 1, 'SPR_PIXEL': 4, 'SPR_TRAIL': 4,
            'SPR_HINT_UP': 2, 'SPR_HINT_RIGHT': 2, 'SPR_CURSOR': 6, 'SPR_STAR': 3,
            'SPR_RING_A': 7, 'SPR_RING_B': 7}
    for i in range(16, 32):
        name = spr[i][0]
        for mode in range(2):
            pal = sp[spal.get(name, 7)] if mode == 0 else dmg_pal()
            draw_tile(img, spr[i][1], 4 * 18 + (i - 16) * 9, mode * 18, pal)
    items.append(('SPRITES', img))
    items.append(('FADE', tiles_row([t for _, t in data['fade']], bg[0], 8)))
    items.append(('LOGO TILES (%d)' % len(data['logo_tiles']),
                  tiles_row(data['logo_tiles'], bg[3], 20)))
    return items


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(ROOT, 'build', 'asset_preview.png'))
    ap.add_argument('--scale', type=int, default=4)
    args = ap.parse_args(argv)
    data = G.load_assets(os.path.join(ROOT, 'assets'), strict=False)
    S = args.scale
    screens = []
    t = title_screen(data)
    lv = level_screen(data)
    screens = [('TITLE DMG', t.render(False)), ('TITLE CGB', t.render(True)),
               ('LEVEL DMG', lv.render(False)), ('LEVEL CGB', lv.render(True))]
    pad = 8 * S
    lab = 12
    sw = 160 * S
    top_h = lab + 144 * S
    items = sheet(data, S)
    sheet_h = sum(lab + im.size[1] * S + pad for _, im in items)
    W = max(4 * sw + 5 * pad, max(im.size[0] * S for _, im in items) + 2 * pad)
    H = pad + top_h + pad + sheet_h
    out = Image.new('RGB', (W, H), SHEET_BG)
    dr = ImageDraw.Draw(out)
    for i, (name, im) in enumerate(screens):
        x = pad + i * (sw + pad)
        dr.text((x, pad - 2), name, fill=LABEL)
        out.paste(im.resize((sw, 144 * S), Image.NEAREST), (x, pad + lab))
    y = pad + top_h + pad
    for name, im in items:
        dr.text((pad, y), name, fill=LABEL)
        out.paste(im.resize((im.size[0] * S, im.size[1] * S), Image.NEAREST), (pad, y + lab))
        y += lab + im.size[1] * S + pad
    d = os.path.dirname(args.out)
    if d and not os.path.isdir(d):
        os.makedirs(d)
    out.save(args.out)
    for name, im in screens:
        pass
    print('wrote %s (%dx%d); logo unique tiles: %d' % (args.out, W, H, len(data['logo_tiles'])))
    return 0


if __name__ == '__main__':
    sys.exit(main())
