"""Regenerate the README media in docs/screens/ by playing the real ROM in PyBoy.

    make rom build/ibgen && python3 tools/screenshots.py
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tests'))
from test_rom import Game, GS_GEN, GS_OVER, GS_TITLE, PS_INTRO, RUN_SECTOR, DIRS, ibgen  # noqa: E402

OUT = os.path.join(ROOT, 'docs', 'screens')
SCALE = 3
SEED = 0xB357


def frame(g, scale=SCALE):
    return g.pb.screen.image.convert('RGB').resize((160 * scale, 144 * scale), 0)


def save(g, name):
    os.makedirs(OUT, exist_ok=True)
    frame(g).save(os.path.join(OUT, name + '.png'))
    print('wrote', name)


def advance_to(g, target):
    while g.u16('run', RUN_SECTOR) < target:
        g.solve_current(SEED)
        g.continue_after_win()


def record_gif(g, sectors, name):
    """Solve `sectors` sectors optimally while recording every other frame."""
    frames = []

    def grab(n):
        for i in range(n):
            g.pb.tick()
            if i % 2 == 0:
                frames.append(frame(g, 2).quantize(64))

    for _ in range(sectors):
        sector = g.u16('run', RUN_SECTOR)
        _, moves = ibgen('solve', SEED, sector)
        grab(20)
        for d in moves:
            g.pb.button_press(DIRS[d]); grab(2); g.pb.button_release(DIRS[d])
            grab(34)
        grab(90)
        g.pb.button_press('a'); grab(2); g.pb.button_release('a')
        for _ in range(400):
            if g.ready():
                break
            if g.u8('ps') == PS_INTRO:
                grab(40)
                g.pb.button_press('a'); grab(2); g.pb.button_release('a')
            grab(2)
    frames[0].save(os.path.join(OUT, name + '.gif'), save_all=True, append_images=frames[1:],
                   duration=33, loop=0, optimize=True)
    print('wrote', name + '.gif', len(frames), 'frames')


def main():
    g = Game(True)
    g.wait(lambda: g.u8('game_state') == GS_TITLE)
    g.run(90)
    save(g, 'title')
    g.start_run(SEED)
    # sector 14 teaches toggle gates: capture its intro banner
    for s in range(1, 14):
        g.solve_current(SEED)
        if s < 13:
            g.continue_after_win()
    g.wait(lambda: g.u8('ps') == PS_INTRO, 4000)
    g.run(30)
    save(g, 'intro')
    g.skip_intro()
    advance_to(g, 19)
    g.run(30)
    save(g, 'play')
    record_gif(g, 2, 'gameplay')
    g.solve_current(SEED)
    g.wait(lambda: g.u8('game_state') == GS_GEN, 900)
    g.run(40)
    save(g, 'best')
    g.continue_after_win()
    # rewind: make a move, then hold B
    for d in 'LRUD':
        before = g.u8('st')
        g.move(d)
        if g.u8('st') != before:
            break
    g.wait(g.ready, 300)
    g.pb.button_press('b')
    g.run(6)
    save(g, 'rewind')
    g.pb.button_release('b')
    g.run(10)
    g.press('start', after=12)
    save(g, 'pause')
    g.stop()

    g = Game(False)
    g.start_run(SEED)
    advance_to(g, 6)
    g.run(30)
    save(g, 'dmg')
    g.stop()

    g = Game(True)
    g.start_run(0x0BAD)
    while g.u8('game_state') != GS_OVER:
        for d in 'LRUD':
            g.wait(lambda: g.ready() or g.u8('game_state') not in (4,), 300)
            if g.u8('game_state') == GS_GEN or g.u8('ps') == 3:
                g.continue_after_win()
            if g.u8('game_state') == 4:
                g.move(d)
    g.wait(lambda: 'SIGNAL LOST' in g.screen_text(), 900)
    g.run(40)
    save(g, 'gameover')
    g.stop()


if __name__ == '__main__':
    main()
