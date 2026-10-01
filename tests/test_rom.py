"""End-to-end tests: boot the real ROM in PyBoy (headless) and play it.

The host build of the puzzle core (build/ibgen) is the oracle: the ROM must
generate byte-identical sectors (this also validates the hand-written SM83
solver loop against the C reference) and the host's optimal solutions must
clear them on-device.

Run: make test-rom   (needs `pip install pyboy`)
"""
import io
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
RUN_CLEARED = 10
DIRS = {'U': 'up', 'R': 'right', 'D': 'down', 'L': 'left'}
SRAM_SIZE = 8192
SAVE_SLOTS = (0xA000, 0xA020)   # primary + backup copy of SaveData
SAVE_BEST_SECTOR = 3            # offset of SaveData.run_best_sector
SAVE_BEST_BESTS, SAVE_BEST_STREAK = 5, 7


def ibgen(*args):
    return subprocess.check_output([IBGEN] + [str(a) for a in args], text=True).split()


class Game:
    def __init__(self, cgb, sram=None):
        # always pass a RAM file so a stray build/*.gb.ram never leaks into a test
        ram = io.BytesIO(sram if sram is not None else bytes(SRAM_SIZE))
        self.pb = PyBoy(ROM, window='null', cgb=cgb, symbols=SYM, sound_emulated=False, ram_file=ram)
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

    def screen_text(self, base=0x9800):
        """Decode the BG map (font tiles are ASCII-32) to find on-screen strings."""
        m = self.pb.memory
        rows = []
        for y in range(18):
            rows.append(''.join(chr(m[base + y * 32 + x] + 32) if m[base + y * 32 + x] < 64 else ' '
                                for x in range(20)))
        return '\n'.join(rows)

    def win_text(self):
        """Same for the window map (HUD, banners, pause box)."""
        return self.screen_text(0x9C00)

    def win_tile(self, x, y):
        return self.pb.memory[0x9C00 + y * 32 + x]

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

    def sram(self):
        return bytes(self.pb.memory[0, 0xA000 + i] for i in range(SRAM_SIZE))

    def win_text(self, rows=8):
        """Decode the window map (the HUD / banners live there)."""
        m = self.pb.memory
        return '\n'.join(''.join(chr(m[0x9C00 + y * 32 + x] + 32) if m[0x9C00 + y * 32 + x] < 64 else ' '
                                 for x in range(20)) for y in range(rows))

    def hint_visible(self, frames=40):
        """True if the hint arrow sprite shows up within `frames` (it blinks)."""
        for _ in range(frames):
            self.run(1)
            if 0 < self.pb.memory[0xFE00 + 7 * 4] < 160:
                return True
        return False

    def abandon(self):
        self.press('start', after=10)
        self.press('up')                  # wraps to ABANDON
        self.press('a', after=5)          # asks SURE?
        self.press('a', after=10)

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
            self.assertLess(g.u16('dbg_gen_frames'), 300)
            _, p = g.solve_current(seed)
            if sector == 3:
                g.shot('win')
            if sector in (5, 14, 18):
                g.shot(f'mech{sector}')
            g.continue_after_win()
            # optimal play keeps the BEST streak alive and never loses energy
            self.assertEqual(g.u8('run', RUN_STREAK), sector)
            # the HUD shows the whole streak (it used to clamp to one digit)
            self.assertEqual(g.win_text().split('\n')[0][18:20], '%02d' % sector)
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
        # seed + build date fit inside the box: its right edge is intact
        box_r = g.win_tile(19, 1)
        for y in range(1, 7):
            self.assertEqual(g.win_tile(19, y), box_r, f'pause box edge broken at row {y}')
        g.press('down')
        g.press('a', after=10)
        self.assertTrue(g.wait(g.ready, 300))
        self.assertEqual(g.u8('st'), start)

    def test_move_buffered_during_slide(self):
        """A direction tapped while still sliding plays when the slide lands."""
        g = self.g
        seed = 0x1D0B
        g.start_run(seed)
        _, moves = ibgen('solve', seed, 1)
        g.pb.button_press(DIRS[moves[0]]); g.run(2); g.pb.button_release(DIRS[moves[0]])
        g.run(2)
        self.assertEqual(g.u8('ps'), PS_SLIDE)
        g.press(DIRS[moves[1]], hold=2, after=1)
        self.assertTrue(g.wait(lambda: g.u8('run', RUN_MOVES) == 2, 400))
        g.run(2)
        self.assertTrue(g.wait(g.ready, 400))
        for d in moves[2:]:
            g.wait(g.ready, 600)
            g.move(d)
        self.assertTrue(g.wait(lambda: g.u8('game_state') == GS_GEN or g.ps_in(PS_WIN), 600))
        self.assertEqual(g.u16('run', RUN_SECTOR), 2)

    def test_hold_b_without_history(self):
        """Holding B on a fresh sector complains once and stops the wave on release."""
        g = self.g
        g.start_run(0x0042)
        g.pb.button_press('b')
        g.run(8)
        self.assertIn('NO HISTORY', g.win_text())
        g.run(60)
        g.pb.button_release('b')
        g.run(3)
        self.assertTrue(g.ready())
        # moves still work right after
        start = g.u8('st')
        for d in 'RDLU':
            g.move(d)
            if g.u8('st') != start:
                break
        self.assertNotEqual(g.u8('st'), start)
        # and the "NO HISTORY" line is gone once you move
        self.assertNotIn('NO HISTORY', g.win_text())

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
        # on a fresh save any clear beats the record; a run that cleared
        # nothing does not (it used to claim NEW RECORD! anyway)
        cleared = g.u16('run', RUN_CLEARED)
        self.assertEqual(g.wait(lambda: 'NEW RECORD!' in g.screen_text(), 120), cleared > 0)
        g.run(70)
        g.shot('gameover')
        g.press('a', after=10)
        self.assertTrue(g.wait(lambda: g.u8('game_state') == GS_TITLE, 600))

    def test_energy_below_par_ends_run(self):
        """With less energy than par at the start, the run is lost: it ends at once."""
        g = self.g
        g.start_run(0x1D0B)
        par = g.u8('level', LEVEL_PAR)
        g.pb.memory[g.addr('run') + RUN_ENERGY] = par - 1
        self.assertTrue(g.wait(lambda: 'PAR > ENERGY' in g.win_text(), 30))
        self.assertTrue(g.wait(lambda: g.u8('game_state') == GS_OVER, 300))
        self.assertEqual(g.u8('run', RUN_MOVES), 0)
        self.assertTrue(g.wait(lambda: 'SIGNAL LOST' in g.screen_text(), 600))

    def test_restart_low_energy_can_rewind(self):
        """After a restart the history still holds later states: no PAR > ENERGY."""
        g = self.g
        seed = 0x1D0B
        g.start_run(seed)
        par, moves = ibgen('solve', seed, 1)
        for d in moves[:-1]:                     # stop one move short of the exit
            g.wait(g.ready, 600)
            g.move(d)
        g.wait(g.ready, 300)
        g.pb.memory[g.addr('run') + RUN_ENERGY] = 1
        g.press('start', after=10)
        g.press('down')
        g.press('a', after=10)                   # RESTART
        self.assertTrue(g.wait(g.ready, 300))
        g.run(60)
        self.assertEqual(g.u8('game_state'), GS_PLAY)
        self.assertNotIn('PAR > ENERGY', g.win_text())
        g.press('b', hold=4, after=30)           # undo the restart...
        self.assertTrue(g.wait(g.ready, 300))
        g.move(moves[-1])                        # ...and finish with the last energy
        self.assertTrue(g.wait(lambda: g.u8('game_state') == GS_GEN or g.ps_in(PS_WIN), 600))

    def test_energy_equal_to_par_still_plays(self):
        g = self.g
        seed = 0x1D0B
        g.start_run(seed)
        par = g.u8('level', LEVEL_PAR)
        g.pb.memory[g.addr('run') + RUN_ENERGY] = par
        g.run(30)
        self.assertEqual(g.u8('game_state'), GS_PLAY)
        g.solve_current(seed)                    # exactly par moves: a win at 0 left over
        g.continue_after_win()
        self.assertEqual(g.u16('run', RUN_SECTOR), 2)

    def test_seed_entry(self):
        g = self.g
        g.wait(lambda: g.u8('game_state') == GS_TITLE)
        g.run(20)
        g.press('down')
        g.press('down')
        g.press('a', after=20)
        self.assertTrue(g.wait(lambda: 'ENTER SEED' in g.screen_text(), 300))
        g.run(10)
        # default shown is 1D0B; set it to 2D0A: first digit up, last digit down
        g.press('up')
        g.press('left')
        g.press('down')
        g.shot('seed')
        g.press('a', after=5)
        self.assertTrue(g.wait(lambda: g.u8('game_state') == GS_PLAY, 3000))
        self.assertEqual(g.u16('run', 1), 0x2D0A)
        g.skip_intro()
        par, _, cells = ibgen('dump', 0x2D0A, 1)
        self.assertEqual(g.level_cells().hex(), cells)

    def test_codex(self):
        g = self.g
        g.wait(lambda: g.u8('game_state') == GS_TITLE)
        g.run(20)
        g.press('up')          # wraps to CODEX
        g.press('a', after=20)
        self.assertEqual(g.u8('game_state'), GS_CODEX)
        self.assertTrue(g.wait(lambda: 'HOW IT WORKS' in g.screen_text(), 300))
        g.press('right', after=5)
        self.assertTrue(g.wait(lambda: 'CODEX' in g.screen_text(), 300))
        g.run(10)
        g.shot('codex')
        g.press('b', after=20)
        self.assertTrue(g.wait(lambda: g.u8('game_state') == GS_TITLE, 200))


    def test_new_record_and_save_survives_reboot(self):
        g = self.g
        seed = 0x1D0B
        g.start_run(seed)
        for _ in range(2):
            g.solve_current(seed)
            g.continue_after_win()
        g.abandon()
        self.assertTrue(g.wait(lambda: 'SIGNAL LOST' in g.screen_text(), 900))
        # the record was already raised by the clears; the banner must still show
        self.assertTrue(g.wait(lambda: 'NEW RECORD' in g.screen_text(), 120))
        self.assertIn('RECORD    0003', g.screen_text())
        sram = g.sram()
        g.stop()

        def title_record(ram):
            self.g = Game(self.CGB, ram)
            self.assertTrue(self.g.wait(lambda: self.g.u8('game_state') == GS_TITLE, 600))
            self.g.run(30)
            return self.g.screen_text().splitlines()[17]

        self.assertIn('RECORD 0003', title_record(sram))
        # a torn write that damages the primary copy falls back to the backup
        bad = bytearray(sram)
        bad[SAVE_SLOTS[0] - 0xA000 + SAVE_BEST_SECTOR] ^= 0x5A
        self.assertIn('RECORD 0003', title_record(bytes(bad)))
        self.assertEqual(self.g.sram()[:0x20], sram[:0x20], 'primary copy repaired')
        self.g.stop()
        # a new run from the same save that does not beat 3 is not a record
        self.g = g = Game(self.CGB, sram)
        g.start_run(seed)
        g.abandon()
        self.assertTrue(g.wait(lambda: 'SIGNAL LOST' in g.screen_text(), 900))
        self.assertFalse(g.wait(lambda: 'NEW RECORD' in g.screen_text(), 120))

    def test_restart_clears_hint_and_banner_clamps(self):
        g = self.g
        seed = 0x1D0B
        g.start_run(seed)
        start = g.u8('st')
        _, sol = ibgen('solve', seed, 1)
        hinted = False
        for d in 'RDLU':
            if d == sol[0]:
                continue
            g.move(d)
            g.wait(g.ready, 300)
            if g.u8('st') == start:
                continue
            g.press('select', after=5)
            if g.hint_visible():
                hinted = True
                break
            g.press('b', hold=3, after=20)
            g.wait(g.ready, 300)
        self.assertTrue(hinted)
        g.press('start', after=10)
        g.press('down')
        g.press('a', after=10)            # RESTART
        self.assertTrue(g.wait(g.ready, 300))
        self.assertEqual(g.u8('st'), start)
        self.assertFalse(g.hint_visible(), 'stale hint arrow after restart')
        # 100+ moves: the clear banner saturates at 99 like the HUD does
        for i, d in enumerate(sol):
            g.wait(g.ready, 600)
            if i == len(sol) - 1:
                g.pb.memory[g.addr('run') + RUN_MOVES] = 150
            g.move(d)
        self.assertTrue(g.wait(lambda: g.u8('game_state') == GS_GEN, 600))
        self.assertIn('MOVES 99', g.win_text())


    def test_hint_not_charged_twice(self):
        """SELECT again with the same hint still up re-shows it for free."""
        g = self.g
        g.start_run(0x1D0B)
        energy = g.u8('run', RUN_ENERGY)
        g.press('select', after=5)
        self.assertTrue(g.hint_visible())
        self.assertEqual(g.u8('run', RUN_ENERGY), energy - 3)
        g.press('select', after=5)
        g.press('select', after=5)
        self.assertEqual(g.u8('run', RUN_ENERGY), energy - 3)
        self.assertTrue(g.hint_visible())
        self.assertIn('MOVES LEFT:', g.win_text())
        # the pause menu's HINT is the same hint: no charge either
        g.press('start', after=10)
        g.press('down'); g.press('down')
        g.press('a', after=10)
        self.assertEqual(g.u8('run', RUN_ENERGY), energy - 3)
        # after a move the next hint is a new one and costs again
        _, sol = ibgen('solve', 0x1D0B, 1)
        g.wait(g.ready, 300)
        g.move(sol[0])
        g.wait(g.ready, 300)
        g.press('select', after=5)
        self.assertEqual(g.u8('run', RUN_ENERGY), energy - 1 - 6)

    def test_abandon_needs_confirm(self):
        g = self.g
        g.start_run(0x1D0B)
        g.press('start', after=10)
        g.press('up')                     # RESUME wraps to ABANDON
        g.press('a', after=10)
        self.assertEqual(g.u8('ps'), PS_PAUSE)
        self.assertEqual(g.u8('game_state'), GS_PLAY)
        self.assertIn('SURE? A=YES', g.win_text())
        g.shot('abandon_confirm')
        # moving off it disarms and restores the label; A on RESUME resumes
        g.press('down')
        self.assertNotIn('SURE?', g.win_text())
        self.assertIn('ABANDON', g.win_text())
        g.press('a', after=10)
        self.assertTrue(g.wait(g.ready, 300))
        # the full confirm still ends the run
        g.abandon()
        self.assertTrue(g.wait(lambda: 'SIGNAL LOST' in g.screen_text(), 900))

    def test_tutorial_lines_and_hud(self):
        g = self.g
        seed = 0x1D0B
        g.start_run(seed)
        # the sector sits left-aligned, clear of the par flag
        self.assertEqual(g.win_text().split('\n')[0][:5], 'S1   ')
        while g.u16('run', RUN_SECTOR) < 4:
            g.solve_current(seed)
            g.continue_after_win()
        # sector 3 teaches stop pads, so the SELECT line lives on sector 4
        self.assertIn('SELECT: HINT', g.win_text())
        # DMG: the live player uses OBP1, a brighter ramp than the walls
        self.assertTrue(g.pb.memory[0xFE03] & 0x10)
        if not self.CGB:
            self.assertEqual(g.pb.memory[0xFF49], 0xC4)


    def test_crash_keeps_full_history(self):
        """A crash never touches the rewind history, even when it is full."""
        g = self.g
        g.start_run(0x1D0B)
        start = g.u8('st')
        cells = g.level_cells()
        # put a pit next to the start (independent of what the generator built there)
        d, off = next((d, o) for d, o in (('L', -1), ('R', 1), ('U', -12), ('D', 12))
                      if cells[start + o] == 0)
        g.pb.memory[g.addr('level') + start + off] = 15
        g.set_u16('hist_n', 256)                 # as if 256 moves were already made
        g.move(d)
        self.assertTrue(g.wait(g.ready, 300))
        self.assertEqual(g.u8('st'), start)
        self.assertEqual(g.u16('hist_n'), 256, 'crash evicted a history entry')
        self.assertEqual(g.u8('run', RUN_MOVES), 1)

    def test_dead_end_hint_is_free(self):
        g = self.g
        g.start_run(0x1D0B)
        cells = g.level_cells()
        a = g.addr('level')
        g.pb.memory[a + cells.index(2)] = 0      # remove the exit: nothing can solve it
        energy = g.u8('run', RUN_ENERGY)
        g.press('select', after=5)
        self.assertTrue(g.wait(lambda: 'DEAD END' in g.win_text(), 120))
        self.assertEqual(g.u8('run', RUN_ENERGY), energy)

    def test_big_numbers_saturate(self):
        g = self.g
        g.start_run(0x1D0B)
        a = g.addr('run') + RUN_SECTOR
        g.pb.memory[a], g.pb.memory[a + 1] = 10000 & 0xFF, 10000 >> 8
        g.press('start', after=10)
        g.press('b', after=10)                   # resume redraws the HUD
        self.assertEqual(g.win_text().split('\n')[0][:5], 'S9999')
        g.abandon()                              # the summary used to show 0000
        self.assertTrue(g.wait(lambda: 'SIGNAL LOST' in g.screen_text(), 900))
        self.assertTrue(g.wait(lambda: 'RECORD' in g.screen_text(), 120))
        self.assertIn('SECTOR    9999', g.screen_text())

    def test_no_record_without_clears(self):
        g = self.g
        g.start_run(0x1D0B)
        g.abandon()
        self.assertTrue(g.wait(lambda: 'SIGNAL LOST' in g.screen_text(), 900))
        self.assertFalse(g.wait(lambda: 'NEW RECORD' in g.screen_text(), 120))

    def test_records_saved_on_clear(self):
        """BEST count and streak records hit SRAM as they happen, not at game over."""
        g = self.g
        seed = 0x1D0B
        g.start_run(seed)
        for _ in range(2):
            g.solve_current(seed)
            g.continue_after_win()
        sram = g.sram()
        for base in SAVE_SLOTS:
            o = base - 0xA000
            self.assertEqual(sram[o + SAVE_BEST_BESTS], 2)
            self.assertEqual(sram[o + SAVE_BEST_STREAK], 2)


class RomTestCGB(RomTest):
    CGB = True
    SECTORS = 12


if __name__ == '__main__':
    unittest.main()
