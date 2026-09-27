"""End-to-end tests: boot the real ROM in PyBoy (headless) and play it.

The host build of the puzzle core (build/ibgen) is the oracle: the ROM must
generate byte-identical sectors (this also validates the hand-written SM83
solver loop against the C reference) and the host's optimal solutions must
clear them on-device.

Run: make test-rom   (needs `pip install pyboy`)
"""
import os
import subprocess
import unittest

from pyboy import PyBoy

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROM = os.path.join(ROOT, 'build', 'infinite-best.gb')
SYM = os.path.join(ROOT, 'build', 'infinite-best.sym')
IBGEN = os.path.join(ROOT, 'build', 'ibgen')
SHOTS = os.path.join(ROOT, 'build', 'screens')

GS_BOOT, GS_TITLE, GS_CODEX, GS_GEN, GS_PLAY, GS_OVER = range(6)
PS_IDLE, PS_SLIDE, PS_DEAD, PS_WIN, PS_PAUSE, PS_INTRO = range(6)
GN = 120
LEVEL_PAR = GN + 1 + 1 + 3 + 2       # offset of Level.par
RUN_SECTOR, RUN_ENERGY, RUN_STREAK, RUN_MOVES = 3, 5, 6, 12
DIRS = {'U': 'up', 'R': 'right', 'D': 'down', 'L': 'left'}


def ibgen(*args):
    return subprocess.check_output([IBGEN] + [str(a) for a in args], text=True).split()


class Game:
    def __init__(self, cgb):
        self.pb = PyBoy(ROM, window='null', cgb=cgb, symbols=SYM, sound_emulated=False)
        self.pb.set_emulation_speed(0)
        self.tag = 'cgb' if cgb else 'dmg'

    def addr(self, name):
        return self.pb.symbol_lookup('_' + name)[1]

    def u8(self, name, off=0):
        return self.pb.memory[self.addr(name) + off]

    def u16(self, name, off=0):
        a = self.addr(name) + off
        return self.pb.memory[a] | self.pb.memory[a + 1] << 8

    def set_u16(self, name, v):
        a = self.addr(name)
        self.pb.memory[a] = v & 0xFF
        self.pb.memory[a + 1] = v >> 8

    def run(self, n=1):
        for _ in range(n):
            self.pb.tick()

    def press(self, button, hold=3, after=3):
        self.pb.button_press(button)
        self.run(hold)
        self.pb.button_release(button)
        self.run(after)

    def wait(self, cond, limit=2000):
        for _ in range(limit):
            if cond():
                return True
            self.pb.tick()
        return False

    def state(self):
        return self.u8('game_state'), self.u8('ps')

    def ready(self):
        return self.state() == (GS_PLAY, PS_IDLE)

    def shot(self, name):
        os.makedirs(SHOTS, exist_ok=True)
        self.pb.screen.image.save(os.path.join(SHOTS, f'{self.tag}_{name}.png'))

    def screen_text(self):
        """Decode the BG map (font tiles are ASCII-32) to find on-screen strings."""
        m = self.pb.memory
        rows = []
        for y in range(18):
            rows.append(''.join(chr(m[0x9800 + y * 32 + x] + 32) if m[0x9800 + y * 32 + x] < 64 else ' '
                                for x in range(20)))
        return '\n'.join(rows)

    def start_run(self, seed):
        assert self.wait(lambda: self.u8('game_state') == GS_TITLE)
        self.run(30)
        self.set_u16('dbg_seed', seed)
        self.press('start')
        # dismiss the intro banner of teaching sectors if any
        assert self.wait(lambda: self.u8('game_state') == GS_PLAY, 3000)
        self.skip_intro()

    def skip_intro(self):
        self.wait(lambda: self.ps_in(PS_IDLE, PS_INTRO), 3000)
        if self.u8('ps') == PS_INTRO:
            self.run(25)
            self.press('a')
        assert self.wait(self.ready, 3000), self.state()

    def ps_in(self, *states):
        return self.u8('game_state') == GS_PLAY and self.u8('ps') in states

    def level_cells(self):
        a = self.addr('level')
        return bytes(self.pb.memory[a + i] for i in range(GN))

    def move(self, d):
        self.press(DIRS[d], hold=2, after=1)
        self.wait(lambda: not self.ps_in(PS_SLIDE), 400)

    def solve_current(self, seed):
        sector = self.u16('run', RUN_SECTOR)
        par, moves = ibgen('solve', seed, sector)
        for d in moves:
            self.wait(self.ready, 600)
            self.move(d)
        return sector, int(par)

    def continue_after_win(self):
        assert self.wait(lambda: self.u8('game_state') == GS_GEN or self.ps_in(PS_WIN), 600)
        self.wait(lambda: self.u8('game_state') == GS_PLAY and self.u8('ps') != PS_WIN, 4000)
        self.skip_intro()

    def stop(self):
        self.pb.stop(save=False)


class RomTest(unittest.TestCase):
    CGB = False
    SECTORS = 20      # reaches every mechanic (portals unlock at 18)

    @classmethod
    def setUpClass(cls):
        for f in (ROM, SYM, IBGEN):
            if not os.path.exists(f):
                raise unittest.SkipTest(f'missing {f}; run make rom build/ibgen')

    def setUp(self):
        self.g = Game(self.CGB)

    def tearDown(self):
        self.g.stop()

    def test_boot_title(self):
        g = self.g
        self.assertTrue(g.wait(lambda: g.u8('game_state') == GS_TITLE, 600))
        g.run(60)
        text = g.screen_text()
        for word in ('RUN', 'ZEN', 'CODEX', 'RECORD'):
            self.assertIn(word, text)
        self.assertEqual(g.u8('is_cgb'), 1 if self.CGB else 0)
        g.shot('title')

    def test_generation_matches_host_and_solutions_clear(self):
        g = self.g
        seed = 0x1D0B
        g.start_run(seed)
        g.shot('sector1')
        for sector in range(1, self.SECTORS + 1):
            self.assertEqual(g.u16('run', RUN_SECTOR), sector)
            par, start, cells = ibgen('dump', seed, sector)
            self.assertEqual(g.level_cells().hex(), cells, f'sector {sector} grid differs from host')
            self.assertEqual(g.u8('level', LEVEL_PAR), int(par))
            self.assertEqual(g.u8('st'), int(start))
            self.assertLess(g.u8('dbg_gen_frames'), 250)
            _, p = g.solve_current(seed)
            if sector == 3:
                g.shot('win')
            if sector in (5, 14, 18):
                g.shot(f'mech{sector}')
            g.continue_after_win()
            # optimal play keeps the BEST streak alive and never loses energy
            self.assertEqual(g.u8('run', RUN_STREAK), sector)
        g.shot('sector7')

    def test_rewind_restores_state(self):
        g = self.g
        seed = 0x0042
        g.start_run(seed)
        start = g.u8('st')
        energy = g.u8('run', RUN_ENERGY)
        for d in 'RDLU':
            before = g.u8('st')
            g.move(d)
            if g.u8('st') != before:
                break
        self.assertTrue(g.wait(g.ready, 300))
        moved = g.u8('st')
        self.assertNotEqual(moved, start)
        self.assertEqual(g.u8('run', RUN_ENERGY), energy - 1)
        self.assertEqual(g.u8('run', RUN_MOVES), 1)
        g.press('b', hold=4, after=2)
        g.run(4)
        g.shot('rewind')
        g.run(10)
        self.assertEqual(g.u8('st'), start)
        # rewinding is free but spent energy stays spent
        self.assertEqual(g.u8('run', RUN_ENERGY), energy - 1)
        self.assertEqual(g.u8('run', RUN_MOVES), 1)

    def test_pause_menu_and_restart(self):
        g = self.g
        g.start_run(0x0777)
        start = g.u8('st')
        for d in 'RDLU':
            g.move(d)
            if g.u8('st') != start:
                break
        g.wait(g.ready, 300)
        g.press('start', after=10)
        self.assertEqual(g.u8('ps'), PS_PAUSE)
        g.shot('pause')
        g.press('down')
        g.press('a', after=10)
        self.assertTrue(g.wait(g.ready, 300))
        self.assertEqual(g.u8('st'), start)

    def test_energy_runs_out(self):
        g = self.g
        g.start_run(0x0BAD)
        for _ in range(200):
            if g.u8('game_state') == GS_OVER:
                break
            for d in 'LRUD':
                if g.u8('game_state') != GS_PLAY:
                    break
                g.wait(lambda: g.ready() or g.u8('game_state') != GS_PLAY, 300)
                if g.u8('game_state') == GS_PLAY:
                    # avoid accidentally winning: undo any move that would finish
                    g.move(d)
                    if g.ps_in(PS_WIN) or g.u8('game_state') == GS_GEN:
                        g.continue_after_win()
        self.assertTrue(g.wait(lambda: g.u8('game_state') == GS_OVER, 600))
        self.assertTrue(g.wait(lambda: 'SIGNAL LOST' in g.screen_text(), 600))
        g.run(70)
        g.shot('gameover')
        g.press('a', after=10)
        self.assertTrue(g.wait(lambda: g.u8('game_state') == GS_TITLE, 600))

    def test_codex(self):
        g = self.g
        g.wait(lambda: g.u8('game_state') == GS_TITLE)
        g.run(20)
        g.press('down')
        g.press('down')
        g.press('a', after=20)
        self.assertEqual(g.u8('game_state'), GS_CODEX)
        self.assertIn('HOW IT WORKS', g.screen_text())
        g.press('right', after=20)
        self.assertIn('CODEX', g.screen_text())
        g.shot('codex')
        g.press('b', after=20)
        self.assertTrue(g.wait(lambda: g.u8('game_state') == GS_TITLE, 200))


class RomTestCGB(RomTest):
    CGB = True
    SECTORS = 12


if __name__ == '__main__':
    unittest.main()
