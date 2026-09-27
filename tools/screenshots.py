"""Regenerate docs/screens/*.png by playing the real ROM in PyBoy (CGB mode).

    make rom build/ibgen && python3 tools/screenshots.py
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tests'))
from test_rom import Game, GS_GEN, GS_PLAY, PS_WIN, RUN_SECTOR  # noqa: E402

OUT = os.path.join(ROOT, 'docs', 'screens')
SCALE = 3


def save(g, name):
    os.makedirs(OUT, exist_ok=True)
    im = g.pb.screen.image.convert('RGB')
    im = im.resize((160 * SCALE, 144 * SCALE), 0)
    im.save(os.path.join(OUT, name + '.png'))
    print('wrote', name)


def main():
    seed = 0x5EED
    g = Game(True)
    g.wait(lambda: g.u8('game_state') == 1)
    g.run(90)
    save(g, 'title')
    g.start_run(seed)
    target = int(sys.argv[1]) if len(sys.argv) > 1 else 16
    while g.u16('run', RUN_SECTOR) < target:
        g.solve_current(seed)
        g.continue_after_win()
    g.run(30)
    save(g, 'play')
    g.solve_current(seed)
    g.wait(lambda: g.u8('game_state') == GS_GEN, 900)
    g.run(40)
    save(g, 'best')
    g.stop()


if __name__ == '__main__':
    main()
