"""Tests for tools/gen_assets.py (stdlib unittest).

Run: python3 -m unittest discover -s tests
"""

import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import gen_assets as G  # noqa: E402


def c_array(src, name):
    """Return the list of integer values in `const <type> name[...] = {...};`."""
    m = re.search(r'const\s+uint(?:8|16)_t\s+%s\s*\[[^\]]*\]\s*=\s*\{(.*?)\};' % name,
                  src, re.S)
    if not m:
        raise AssertionError('array %s not found' % name)
    body = re.sub(r'/\*.*?\*/', '', m.group(1), flags=re.S)
    return [int(v, 0) for v in body.replace('\n', ' ').split(',') if v.strip()]


class TestGenAssets(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix='ib_assets_')
        rc = G.main(['--assets', os.path.join(ROOT, 'assets'), '--out-dir', cls.tmp])
        assert rc == 0, 'generator failed'
        with open(os.path.join(cls.tmp, 'assets.h')) as f:
            cls.h = f.read()
        with open(os.path.join(cls.tmp, 'assets.c')) as f:
            cls.c = f.read()
        cls.data = G.load_assets(os.path.join(ROOT, 'assets'))

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def define(self, name):
        m = re.search(r'^#define\s+%s\s+(\S+)' % name, self.h, re.M)
        self.assertIsNotNone(m, 'missing #define %s' % name)
        return m.group(1)

    def test_header_defines(self):
        expect = {'TILE_FONT_BASE': '0', 'TILE_UI_BASE': '64', 'TILE_MT_BASE': '80',
                  'TILE_SPR_BASE': '160', 'TILE_FADE_BASE': '192',
                  'NUM_FADE_TILES': '8'}
        for k, v in expect.items():
            self.assertEqual(self.define(k), v)
        for k in ('LOGO_W', 'LOGO_H', 'LOGO_NTILES', 'PAL_UI', 'PAL_HUD_ACCENT',
                  'PAL_LOGO', 'SPAL_PLAYER', 'SPAL_SPARK', 'SPAL_HINT', 'SPAL_STAR',
                  'TILE_SOLID3'):
            self.define(k)
        self.assertIn('#include <stdint.h>', self.h)
        self.assertIn('#include "assets.h"', self.c)
        for sym in (['NUM_MT'] + G.MT_ORDER + G.UI_REQUIRED + G.SPR_REQUIRED +
                    ['UI_BOX_B', 'UI_BOX_R']):
            self.assertRegex(self.h, r'\b%s\b' % sym)
        for decl in ('font_tiles[64*16]', 'ui_tiles[16*16]', 'mt_tiles[NUM_MT*4*16]',
                     'spr_tiles[32*16]', 'fade_tiles[NUM_FADE_TILES*16]',
                     'logo_tiles[LOGO_NTILES*16]', 'logo_map[LOGO_W*LOGO_H]',
                     'mt_cgb_pal[NUM_MT]', 'logo_cgb_attr[LOGO_W*LOGO_H]'):
            self.assertIn('extern const uint8_t %s;' % decl, self.h)
        self.assertIn('extern const uint16_t bg_cgb_pal[8*4];', self.h)
        self.assertIn('extern const uint16_t spr_cgb_pal[8*4];', self.h)

    def test_mt_enum_order(self):
        body = re.search(r'enum \{\s*MT_FLOOR.*?\};', self.h, re.S).group(0)
        names = re.findall(r'\b(MT_[A-Z0-9_]+)\b', body)
        self.assertEqual(names, G.MT_ORDER)
        self.assertEqual(len(G.MT_ORDER), 20)

    def test_counts_and_sizes(self):
        n_mt = len(G.MT_ORDER)
        sizes = {'font_tiles': 64 * 16, 'ui_tiles': 16 * 16, 'mt_tiles': n_mt * 64,
                 'spr_tiles': 32 * 16, 'fade_tiles': 8 * 16}
        for name, n in sizes.items():
            vals = c_array(self.c, name)
            self.assertEqual(len(vals), n, name)
            self.assertTrue(all(0 <= v <= 255 for v in vals), name)
        lw, lh, nt = (int(self.define(k)) for k in ('LOGO_W', 'LOGO_H', 'LOGO_NTILES'))
        self.assertLessEqual(lw, 20)
        self.assertLessEqual(nt, 80)
        self.assertEqual(len(c_array(self.c, 'logo_tiles')), nt * 16)
        self.assertEqual(len(c_array(self.c, 'logo_map')), lw * lh)
        self.assertEqual(len(c_array(self.c, 'logo_cgb_attr')), lw * lh)
        self.assertEqual(len(c_array(self.c, 'mt_cgb_pal')), n_mt)
        self.assertEqual(len(c_array(self.c, 'bg_cgb_pal')), 32)
        self.assertEqual(len(c_array(self.c, 'spr_cgb_pal')), 32)

    def test_tile_rows_valid(self):
        d = self.data
        tiles = list(d['font']) + [t for _, t in d['ui']] + \
            [t for _, ts, _ in d['mt'] for t in ts] + [t for _, t in d['spr']] + \
            [t for _, t in d['fade']] + list(d['logo_tiles'])
        for t in tiles:
            self.assertEqual(len(t), 8)
            for row in t:
                self.assertEqual(len(row), 8)
                self.assertTrue(all(v in (0, 1, 2, 3) for v in row))
        # every source pixel row in assets/ is 8n wide and uses only .123
        for fn in sorted(f for f in os.listdir(os.path.join(ROOT, 'assets')) if f.endswith('.txt')):
            for b in G.parse_file(os.path.join(ROOT, 'assets', fn)):
                if b.kind in ('bgpal', 'sprpal', 'logo_attr'):
                    continue
                self.assertEqual(b.w % 8, 0, b.where)
                self.assertEqual(b.h % 8, 0, b.where)
                for r in b.rows:
                    self.assertTrue(set(r) <= set('.123'), b.where)

    def test_encoding(self):
        t = [[0, 1, 2, 3, 0, 0, 0, 3]] + [[0] * 8] * 7
        e = G.encode_tile(t)
        self.assertEqual(len(e), 16)
        self.assertEqual(e[0], 0b01010001)  # low bits
        self.assertEqual(e[1], 0b00110001)  # high bits

    def test_space_and_fade(self):
        font = c_array(self.c, 'font_tiles')
        self.assertEqual(font[:16], [0] * 16)
        fade = c_array(self.c, 'fade_tiles')
        self.assertEqual(fade[:16], [0] * 16)
        self.assertEqual(fade[-16:], [0xFF] * 16)
        # density increases monotonically
        dens = [sum(bin(b).count('1') for b in fade[i * 16:(i + 1) * 16]) for i in range(8)]
        self.assertEqual(dens, sorted(dens))

    def test_no_tile_index_overflow(self):
        lw, lh, nt = (int(self.define(k)) for k in ('LOGO_W', 'LOGO_H', 'LOGO_NTILES'))
        for v in c_array(self.c, 'logo_map'):
            self.assertTrue(v < 80 or 80 <= v < 80 + nt, v)
            self.assertLess(v, 160)
        for v in c_array(self.c, 'logo_cgb_attr') + c_array(self.c, 'mt_cgb_pal'):
            self.assertTrue(0 <= v <= 7)
        ui = [int(x) for x in re.findall(r'UI_\w+ = TILE_UI_BASE \+ (\d+)', self.h)]
        self.assertTrue(ui and max(ui) < 16)
        spr = [int(x) for x in re.findall(r'SPR_\w+ = TILE_SPR_BASE \+ (\d+)', self.h)]
        self.assertTrue(spr and max(spr) < 32)
        self.assertLessEqual(G.TILE_MT_BASE + len(G.MT_ORDER) * 4, G.TILE_SPR_BASE)
        self.assertLessEqual(G.TILE_FADE_BASE + 8, 256)

    def test_palettes(self):
        bg = c_array(self.c, 'bg_cgb_pal')
        for v in bg + c_array(self.c, 'spr_cgb_pal'):
            self.assertLess(v, 0x8000)
        self.assertEqual(len(set(bg[i * 4] for i in range(8))), 1,
                         'all BG palettes must share colour 0')

    def test_deterministic(self):
        tmp2 = tempfile.mkdtemp(prefix='ib_assets2_')
        try:
            G.main(['--assets', os.path.join(ROOT, 'assets'), '--out-dir', tmp2])
            for fn in ('assets.h', 'assets.c'):
                with open(os.path.join(tmp2, fn)) as f:
                    self.assertEqual(f.read(), self.h if fn == 'assets.h' else self.c)
        finally:
            shutil.rmtree(tmp2, ignore_errors=True)

    def test_committed_output_up_to_date(self):
        p = os.path.join(ROOT, 'src', 'gb', 'assets.c')
        if not os.path.exists(p):
            self.skipTest('src/gb/assets.c not generated')
        with open(p) as f:
            self.assertEqual(f.read(), self.c, 'run python3 tools/gen_assets.py')

    def test_gcc_compiles(self):
        gcc = shutil.which('gcc')
        if not gcc:
            self.skipTest('gcc not installed')
        out = os.path.join(self.tmp, 'a.o')
        r = subprocess.run([gcc, '-std=c99', '-Wall', '-Wextra', '-Werror', '-c',
                            '-o', out, os.path.join(self.tmp, 'assets.c')],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.assertEqual(r.returncode, 0, r.stdout.decode())


if __name__ == '__main__':
    unittest.main()
