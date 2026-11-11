"""Hardware-accuracy tests: the ROM on headless mGBA, one instruction at a time.

PyBoy accepts things a real Game Boy does not, so a ROM can pass every PyBoy test and
still glitch on a cartridge. mGBA is cycle-accurate, and tools/mgba/mgba_server.c steps
it instruction by instruction to check what a console would do:

  * VRAM and CGB palette writes made while the LCD is drawing (mode 3) are dropped by
    the hardware, leaving a wrong tile on screen. Every such store is watched.
  * The rewind wave and the title logo's ripple are raster effects: they need their
    LYC interrupt on every band of every frame, however long the VBlank handler runs.
  * The whole PyBoy suite (tests/test_rom.py) is run again on mGBA, DMG and CGB.

Run: make test-hw   (needs libmgba-dev; skipped with a notice if build/mgba_server
is missing)
"""
import os
import random
import unittest

import test_rom
from test_rom import (Game, GS_GEN, GS_OVER, GS_PLAY, GS_TITLE, PS_WIN, RUN_ENERGY, RUN_SECTOR,
                      DIRS, ibgen)

VBLANK_DOTS = 4560      # 10 scanlines of 456 dots
WAVE_BANDS = 33         # play field: one LYC interrupt per 4 lines over 128, plus the reset
LOGO_BANDS = 15


def need_mgba():
    for f in (test_rom.ROM, test_rom.SYM, test_rom.IBGEN):
        if not os.path.exists(f):
            raise unittest.SkipTest(f'missing {f}; run make rom build/ibgen')
    if not os.path.exists(test_rom.MGBA):
        raise unittest.SkipTest('build/mgba_server missing (make mgba; needs libmgba-dev)')


class HwTest(unittest.TestCase):
    CGB = False

    @classmethod
    def setUpClass(cls):
        need_mgba()

    def setUp(self):
        self.g = Game(self.CGB, emu='mgba')

    def tearDown(self):
        self.g.stop()

    def test_no_vram_write_is_dropped(self):
        """Tour every screen with the write watcher on: nothing may be refused by the PPU."""
        g = self.g
        g.pb.watch()
        seed = 0x1D0B
        rnd = random.Random(5)
        g.wait(lambda: g.u8('game_state') == GS_TITLE)
        g.run(120)
        g.press('up'); g.press('a', after=30)                    # codex, both pages
        g.press('right', after=30); g.press('left', after=30); g.press('b', after=40)
        g.press('up'); g.press('a', after=30)                    # seed entry
        for b in ('up', 'left', 'down', 'right'):
            g.press(b, after=6)
        g.press('b', after=40)
        g.press('down'); g.press('down')                         # SEED -> CODEX -> RUN
        g.start_run(seed)
        while g.u16('run', RUN_SECTOR) < 9:
            sector = g.u16('run', RUN_SECTOR)
            g.pb.memory[g.addr('run') + RUN_ENERGY] = 55
            _, moves = ibgen('solve', seed, sector)
            for d in moves[:-1]:
                g.wait(g.ready, 600)
                g.move(d)
            g.wait(g.ready, 600)
            # wander, with rewinds started at odd moments (mid-slide, on landing)
            for d in 'URDL':
                if g.ps_in(PS_WIN) or g.u8('game_state') != GS_PLAY:
                    break
                g.press(DIRS[d], hold=2, after=rnd.randint(0, 12))
                g.pb.button_press('b'); g.run(rnd.randint(3, 25)); g.pb.button_release('b')
                g.run(rnd.randint(1, 4))
            if g.u8('game_state') == GS_PLAY and not g.ps_in(PS_WIN):
                g.wait(g.ready, 600)
                self.assertEqual(g.screen_errors(), [], f'sector {sector} after rewinds')
                g.press('select', after=20)                      # hint
                g.press('start', after=20); g.press('down', after=4); g.press('a', after=20)
                g.wait(g.ready, 600)
                g.pb.button_press('b'); g.run(40); g.pb.button_release('b'); g.run(3)
                g.wait(g.ready, 600)
                self.assertEqual(g.screen_errors(), [], f'sector {sector} after restart + rewind')
                if sector == 8:                                  # crash: the glitch tear
                    cells, start = g.level_cells(), g.u8('st')
                    d, off = next((d, o) for d, o in (('L', -1), ('R', 1), ('U', -12), ('D', 12))
                                  if cells[start + o] == 0)
                    g.pb.memory[g.addr('level') + start + off] = 15
                    g.move(d)
                    g.wait(g.ready, 600)
                    g.pb.memory[g.addr('level') + start + off] = 0
                g.solve_current(seed)
            g.continue_after_win()
        g.abandon()
        self.assertTrue(g.wait(lambda: 'SIGNAL LOST' in g.screen_text(), 900))
        g.run(100)
        g.press('a', after=60)
        g.press('down'); g.press('a', after=10)                  # ZEN, and skip twice
        self.assertTrue(g.wait(lambda: g.u8('game_state') == GS_PLAY, 3000))
        g.skip_intro()
        for _ in range(2):
            g.press('start', after=20); g.press('down', after=4); g.press('down', after=4)
            g.press('a', after=10)
            self.assertTrue(g.wait(lambda: g.u8('game_state') == GS_GEN, 300))
            self.assertTrue(g.wait(lambda: g.u8('game_state') == GS_PLAY, 3000))
            g.skip_intro()
            self.assertEqual(g.screen_errors(), [], 'after a ZEN skip')
        self.assertEqual(g.u8('dbg_vbl_late'), 0)
        self.assertGreater(g.pb.stores, 30000, 'the watcher saw too few VRAM stores to mean anything')
        self.assertEqual(g.pb.dropped, 0, f'VRAM writes dropped in mode 3 (pc:addr): {g.pb.drop_log[:16]}')

    def stat_irqs_per_frame(self, frames):
        g = self.g
        g.pb.watch()
        g.pb.isr_max()
        counts, longest = [], 0
        for _ in range(frames):
            g.run(1)
            vbl, _, n = g.pb.isr_max()
            counts.append(n)
            longest = max(longest, vbl)
        g.pb.watch(False)
        return counts, longest

    def test_wave_runs_on_every_frame(self):
        """The raster effects get all their LYC interrupts, even on frames where the sound
        driver keeps the VBlank handler busy long past the end of VBlank."""
        g = self.g
        g.wait(lambda: g.u8('game_state') == GS_TITLE)
        g.run(30)
        counts, _ = self.stat_irqs_per_frame(240)
        self.assertEqual(set(counts), {LOGO_BANDS}, 'title logo ripple lost bands on some frames')
        g.start_run(0x1D0B)
        g.pb.button_press('b')
        g.run(5)
        counts, longest = self.stat_irqs_per_frame(300)
        g.pb.button_release('b')
        self.assertEqual(set(counts), {WAVE_BANDS}, 'rewind wave lost bands on some frames')
        if not self.CGB:
            # the premise of the test: on a DMG the handler does outlast VBlank
            self.assertGreater(longest, VBLANK_DOTS)


class HwTestCGB(HwTest):
    CGB = True


class RomOnMgbaDMG(test_rom.RomTest):
    EMU = 'mgba'

    @classmethod
    def setUpClass(cls):
        need_mgba()


class RomOnMgbaCGB(RomOnMgbaDMG):
    CGB = True
    SECTORS = 12


if __name__ == '__main__':
    unittest.main()
