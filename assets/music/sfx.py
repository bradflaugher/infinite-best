# INFINITE BEST - sound effects.  Compiled by tools/gen_music.py.
#
# Each effect is a Timeline of per-frame register writes.  Helpers (from
# gen_music.py):
#   P1(note, duty, env, sweep)  trigger pulse 1     P1F(note) change pitch only
#   P2(note, duty, env)         trigger pulse 2     P2F(note)
#   P1D/P2D(duty)               change duty         NOI(poly, env) trigger noise
#   NOIP(poly)                  change noise poly   OFF1/OFF2/OFF4  channel off
# note may be a name ("e6"), a midi number, hz=... or reg=... (11-bit value).
#
# SFX[name] = (timeline, channel mask, priority)
#   mask bit0 = pulse1, bit1 = pulse2, bit3 = noise  (the wave channel is
#   never used by sfx, so the bass line always keeps playing).
#   A new sfx is rejected while an overlapping sfx of higher priority plays;
#   equal priority retriggers.  The channels are released to the music when
#   the timeline ends.

P1_, P2_, NO_ = 1, 2, 8

def T():
    return Timeline()

def _lcg(seed):
    s = [seed]
    def nxt():
        s[0] = (s[0] * 1103515245 + 12345) & 0x7FFFFFFF
        return s[0] >> 16
    return nxt

SFX = {}

# MOVE - quick up-sweep "fwip" (hardware sweep up, overflows -> auto-silence)
t = T().at(0, P1("e4", duty=1, env=0xA1, sweep=0x15)).at(7, OFF1())
SFX["MOVE"] = (t, P1_, 1)

# BUMP - thud: low pulse with downward sweep + low noise
t = (T().at(0, P1("a2", duty=2, env=0xC1, sweep=0x1B) + NOI(0x64, 0xB1))
        .at(2, NOIP(0x76)).at(4, NOIP(0x77)).length(10))
SFX["BUMP"] = (t, P1_ | NO_, 2)

# CHIP - bright ascending data blip, E major, with a tiny high echo
t = T()
for i, n in enumerate(["b5", "e6", "g#6", "b6"]):
    t.at(i * 2, P1(n, duty=2, env=0xB1))
t.at(8, P1("e7", duty=1, env=0x92)).at(10, P1D(0)).at(12, P1D(1))
t.at(15, P1("e7", duty=0, env=0x41)).at(17, P1F("b6")).length(26)
SFX["CHIP"] = (t, P1_, 4)

# SWITCH - click-clack
t = (T().at(0, NOI(0x10, 0xA1) + P1("c6", duty=0, env=0x91)).at(1, OFF1())
        .at(5, NOI(0x31, 0x81) + P1("g5", duty=0, env=0x81)).at(6, OFF1()).length(11))
SFX["SWITCH"] = (t, P1_ | NO_, 3)

# PORTAL - rising warble (pitch LFO written every frame)
t = T().at(0, P1("c5", duty=2, env=0xA3))
for f in range(1, 32):
    hz = 523.25 * (2.0 ** (f / 30.0)) * (1.0 + 0.07 * math.sin(f * 2 * math.pi / 5.0))
    t.at(f, P1F(hz=hz))
    if f % 4 == 0:
        t.at(f, P1D(1 + ((f // 4) & 1)))
t.length(34)
SFX["PORTAL"] = (t, P1_, 4)

# ARROW - short tick/redirect
t = T().at(0, P1("b5", duty=1, env=0x91, sweep=0x16)).at(2, P1F("f#6")).at(4, OFF1()).length(5)
SFX["ARROW"] = (t, P1_, 2)

# PIT - glitchy crunch death: random noise poly + stuttering falling pulse
t = T()
rnd = _lcg(77)
t.at(0, NOI(0x21, 0xF3) + P1("c6", duty=3, env=0xF3, sweep=0x00))
pitch = 1.0
for f in range(1, 34):
    r = rnd()
    poly = [0x21, 0x3A, 0x0B, 0x45, 0x2D, 0x56, 0x19, 0x67][r & 7]
    t.at(f, NOIP(poly))
    if f % 3 == 0:
        pitch *= 0.84
        hz = 1046.5 * pitch * (1.3 if (r >> 3) & 1 else 1.0)
        t.at(f, P1F(hz=max(70.0, hz)) + P1D((r >> 4) & 3))
t.at(16, NOI(0x57, 0x94) + P1(hz=150, duty=0, env=0x93, sweep=0x2F))
t.length(38)
SFX["PIT"] = (t, P1_ | NO_, 6)

# REWIND - reverse whoosh: swelling envelopes (volume goes UP), falling
# pulse, noise getting brighter, then an abrupt cut like reversed tape.
t = T().at(0, NOI(0x57, 0x19) + P1("b5", duty=2, env=0x19))
for f in range(1, 14):
    t.at(f, P1F(hz=987.8 * (2.0 ** (-f / 10.0))))
    t.at(f, NOIP([0x57, 0x56, 0x46, 0x45, 0x35, 0x34, 0x24, 0x23, 0x22, 0x12, 0x11, 0x10, 0x00][f - 1]))
t.at(14, OFF1() + OFF4()).length(15)
SFX["REWIND"] = (t, P1_ | NO_, 4)

# MENU - tiny cursor tick
t = T().at(0, P1("e6", duty=1, env=0x91)).at(1, P1F("e7")).at(3, OFF1()).length(4)
SFX["MENU"] = (t, P1_, 3)

# SELECT - confirm: fifth up
t = T().at(0, P1("e6", duty=2, env=0xA1)).at(4, P1("b6", duty=2, env=0xA3)).length(18)
SFX["SELECT"] = (t, P1_, 5)

# WIN - level clear: G major arpeggio fanfare in thirds, ringing final chord
t = T()
for i, (a, b) in enumerate([("g5", "d5"), ("b5", "g5"), ("d6", "b5"), ("g6", "d6")]):
    t.at(i * 4, P1(a, duty=2, env=0xC2) + P2(b, duty=1, env=0x92))
t.at(18, P1("b6", duty=2, env=0xB5) + P2("g6", duty=1, env=0x85))
for f in range(24, 50):
    t.at(f, P1F(hz=1975.5 * (1.0 + 0.012 * math.sin((f - 24) * 2 * math.pi / 7.0))))
t.at(30, P1D(1)).length(52)
SFX["WIN"] = (t, P1_ | P2_, 7)

# BEST - perfect-par fanfare: rising I-ii-iii-IV-V in D with kicks, crash,
# then a big ringing D chord with vibrato and a sparkle on top.
t = T()
segs = [(["d5", "f#5", "a5"], ["a4", "d5", "f#5"]),
        (["e5", "g5", "b5"], ["b4", "e5", "g5"]),
        (["f#5", "a5", "d6"], ["d5", "f#5", "a5"]),
        (["g5", "b5", "e6"], ["e5", "g5", "b5"]),
        (["a5", "c#6", "e6"], ["e5", "a5", "c#6"])]
f = 0
for up, lo in segs:
    t.at(f, NOI(0x31 if f else 0x02, 0xA1 if f else 0xC4))
    for j in range(3):
        t.at(f + j * 3, P1(up[j], duty=2, env=0xC1) + P2(lo[j], duty=1, env=0x91))
    f += 9
t.at(f, P1("d6", duty=2, env=0xD7) + P2("f#6", duty=1, env=0xA6) + NOI(0x02, 0xA6))
t.at(f + 6, P1("a6", duty=2, env=0xC7))
for g in range(f + 8, f + 30):
    t.at(g, P1F(hz=1760.0 * (1.0 + 0.012 * math.sin((g - f) * 2 * math.pi / 7.0))))
t.at(f + 12, P1D(1)).at(f + 18, P2("d7", duty=0, env=0x63)).at(f + 22, P2("a6", duty=0, env=0x43))
t.length(f + 32)
SFX["BEST"] = (t, P1_ | P2_ | NO_, 8)

# UNLOCK - exit-open chime on pulse 2 (can layer with CHIP on pulse 1)
t = (T().at(0, P2("e6", duty=2, env=0xA4)).at(5, P2("b6", duty=2, env=0x94))
        .at(10, P2("e7", duty=0, env=0x76)).at(20, P2D(1)).length(40))
SFX["UNLOCK"] = (t, P2_, 5)

# GEN - data blip for the generation screen (retriggerable)
t = T().at(0, P1("c7", duty=1, env=0x81)).at(1, P1F("g6")).at(2, OFF1()).length(3)
SFX["GEN"] = (t, P1_, 1)

# NEWMECH - mysterious discovery: whole-tone climb with a delayed echo,
# landing on a tritone (C + F#) that shimmers.
t = T()
wt = ["c6", "d6", "e6", "f#6", "g#6", "a#6"]
for i, n in enumerate(wt):
    t.at(i * 5, P1(n, duty=0, env=0x93))
    t.at(i * 5 + 3, P2(midi_of(n) - 12, duty=2, env=0x52))
t.at(31, P1("c7", duty=0, env=0xA6) + P2("f#6", duty=1, env=0x76))
for g in range(33, 62):
    t.at(g, P1F(hz=2093.0 * (1.0 + 0.010 * math.sin((g - 33) * 2 * math.pi / 9.0))))
t.at(40, P2D(2)).at(48, P2D(1)).length(64)
SFX["NEWMECH"] = (t, P1_ | P2_, 6)

# HINT - soft ping on pulse 2
t = T().at(0, P2("a6", duty=2, env=0x74)).at(9, P2("e7", duty=0, env=0x43)).length(30)
SFX["HINT"] = (t, P2_, 3)

# ERROR - two short rough buzzes (not allowed)
t = T()
for start in (0, 11):
    t.at(start, P1(hz=110.0, duty=0, env=0xB0))
    for k in range(1, 8):
        t.at(start + k, P1F(hz=110.0 if k & 1 == 0 else 104.0))
    t.at(start + 8, OFF1())
t.length(20)
SFX["ERROR"] = (t, P1_, 5)
