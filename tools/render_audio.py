#!/usr/bin/env python3
"""render_audio.py - build a tiny sound-test ROM and render songs/sfx to WAV
with PyBoy (headless), plus measure sound_tick() cost on the emulated CPU.

    python3 tools/render_audio.py                 # all songs + sfx -> build/audio/*.wav
    python3 tools/render_audio.py --song 1 --secs 60
    python3 tools/render_audio.py --bench         # cycle stats only

Needs /opt/gbdk and `pip install pyboy numpy`.  Output goes to build/.
"""
import argparse
import os
import subprocess
import sys
import wave

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, "build", "sndtest")
LCC = os.environ.get("LCC", "/opt/gbdk/bin/lcc")

SONGS = ["title", "run_a", "run_b", "zen", "gameover"]
SFX = ["move", "bump", "chip", "switch", "portal", "arrow", "pit", "rewind", "menu",
       "select", "win", "best", "unlock", "gen", "newmech", "hint", "error"]

MAIN_C = r"""
#include <gb/gb.h>
#include <stdint.h>
#include "sound.h"
/* mailbox at 0xD800: [0]=song req (0xFF none) [1]=sfx req [2]=rewind [3]=muffle
   [8..9]=last tick cost [10..11]=max cost [12..15]=sum (units of 16 M-cycles) */
#define MB ((volatile uint8_t *)0xD800)
void main(void)
{
    uint16_t cost, mx = 0; uint32_t sum = 0;
    MB[0] = 0xFF; MB[1] = 0xFF; MB[2] = 0; MB[3] = 0;
    sound_init();
    TAC_REG = 0x06;                    /* 65536 Hz: 1 count = 64 T = 16 M-cycles */
    while (1) {
        wait_vbl_done();
        if (MB[0] != 0xFF) { if (MB[0] == 0xFE) music_stop(); else music_play(MB[0]); MB[0] = 0xFF; }
        if (MB[1] != 0xFF) { sfx_play(MB[1]); MB[1] = 0xFF; }
        music_set_rewind(MB[2]);
        music_set_muffle(MB[3]);
        disable_interrupts();
        TIMA_REG = 0; IF_REG &= ~TIM_IFLAG;
        sound_tick();
        cost = TIMA_REG;
        if (IF_REG & TIM_IFLAG) cost += 256;
        enable_interrupts();
        if (cost > mx) mx = cost;
        sum += cost;
        MB[8] = cost; MB[9] = cost >> 8; MB[10] = mx; MB[11] = mx >> 8;
        MB[12] = sum; MB[13] = sum >> 8; MB[14] = sum >> 16; MB[15] = sum >> 24;
    }
}
"""


def build_rom():
    os.makedirs(BUILD, exist_ok=True)
    main = os.path.join(BUILD, "main.c")
    with open(main, "w") as f:
        f.write(MAIN_C)
    rom = os.path.join(BUILD, "sndtest.gb")
    src = os.path.join(ROOT, "src", "gb")
    cmd = [LCC, "-Wm-yC", "-Wm-yt0x19", "-autobank", "-I" + src, "-o", rom, main,
           os.path.join(src, "sound.c"), os.path.join(src, "music_data.c")]
    subprocess.check_call(cmd)
    return rom


class Rig:
    def __init__(self, rom):
        from pyboy import PyBoy
        self.pb = PyBoy(rom, window="null", sound_emulated=True, sound_sample_rate=48000)
        self.m = self.pb.memory
        self.pb.tick(400, False, True)  # past the boot ROM
        self.samples = []

    def frame(self, n=1, keep=True):
        import numpy as np
        for _ in range(n):
            self.pb.tick(1, False, True)
            if keep:
                self.samples.append(np.array(self.pb.sound.ndarray, copy=True))

    def cmd(self, idx, val):
        self.m[0xD800 + idx] = val

    def cost(self):
        m = self.m
        last = m[0xD808] | m[0xD809] << 8
        mx = m[0xD80A] | m[0xD80B] << 8
        s = m[0xD80C] | m[0xD80D] << 8 | m[0xD80E] << 16 | m[0xD80F] << 24
        return last * 16, mx * 16, s * 16

    def take(self):
        import numpy as np
        a = np.concatenate(self.samples) if self.samples else np.zeros((0, 2), dtype=np.int8)
        self.samples = []
        return a


def write_wav(path, a, rate=48000):
    import numpy as np
    pcm = (a.astype(np.int16) * 256).astype("<i2")
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm.tobytes())


def stats(a):
    import numpy as np
    if len(a) == 0:
        return "empty"
    x = a.astype(np.float64)
    rms = np.sqrt((x ** 2).mean())
    peak = np.abs(x).max()
    clip = (np.abs(a.astype(np.int16)) >= 127).mean() * 100
    return "rms %5.1f  peak %3d  clip %.2f%%" % (rms, peak, clip)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--song", type=int, default=None)
    ap.add_argument("--secs", type=float, default=None)
    ap.add_argument("--bench", action="store_true")
    args = ap.parse_args()
    rom = build_rom()
    out = os.path.join(ROOT, "build", "audio")
    os.makedirs(out, exist_ok=True)
    rig = Rig(rom)

    songs = [args.song] if args.song is not None else range(len(SONGS))
    worst = 0
    for s in songs:
        secs = args.secs or (6 if SONGS[s] == "gameover" else 80)
        rig.cost()
        rig.cmd(0, s)
        rig.frame(2, keep=False)
        base = rig.cost()[2]
        # reset max by rebuilding not possible; track via deltas
        n = int(secs * 60)
        peak = 0
        costs = []
        for i in range(n):
            rig.frame(1, keep=not args.bench)
            costs.append(rig.cost()[0])
            peak = max(peak, costs[-1])
        costs.sort()
        total = rig.cost()[2] - base
        worst = max(worst, peak)
        a = rig.take()
        if not args.bench:
            write_wav(os.path.join(out, "song_%d_%s.wav" % (s, SONGS[s])), a)
        print("song %-9s %5.1fs  tick avg %4d median %4d p90 %4d max %4d M-cycles  %s" %
              (SONGS[s], secs, total // n, costs[n // 2], costs[n * 9 // 10], peak,
               "" if args.bench else stats(a)))
        rig.cmd(0, 0xFE)
        rig.frame(10, keep=False)
    if args.song is None:
        # sfx: over silence, then over music
        for i, name in enumerate(SFX):
            rig.cmd(1, i)
            peak = 0
            for _ in range(90):
                rig.frame(1)
                peak = max(peak, rig.cost()[0])
            worst = max(worst, peak)
            a = rig.take()
            if not args.bench:
                write_wav(os.path.join(out, "sfx_%02d_%s.wav" % (i, name)), a)
            print("sfx  %-9s tick max %4d M-cycles  %s" % (name, peak, "" if args.bench else stats(a)))
        # rewind + muffle demo on RUN_A
        rig.cmd(0, 1)
        rig.frame(240)
        rig.cmd(2, 1)
        rig.cmd(1, SFX.index("rewind"))
        peak = 0
        for _ in range(180):
            rig.frame(1)
            peak = max(peak, rig.cost()[0])
        rig.cmd(2, 0)
        rig.frame(180)
        rig.cmd(3, 1)
        rig.frame(180)
        rig.cmd(1, SFX.index("menu"))
        rig.frame(60)
        rig.cmd(3, 0)
        rig.frame(120)
        a = rig.take()
        worst = max(worst, peak)
        if not args.bench:
            write_wav(os.path.join(out, "fx_rewind_muffle.wav"), a)
        print("fx rewind/muffle demo: rewind tick max %d M-cycles  %s" % (peak, stats(a)))
    print("worst sound_tick: %d M-cycles (%.1f%% of a 17556 M-cycle frame)" % (worst, worst * 100.0 / 17556))


if __name__ == "__main__":
    main()
