# INFINITE BEST - song sources.  Compiled by tools/gen_music.py.
#
# Notation: see the docstring at the top of tools/gen_music.py.
# One row = one 16th note, 16 rows per bar.  speed = frames per row.
# Channels: p1 = pulse 1 (lead), p2 = pulse 2 (arps / harmony),
#           wv = wave (bass), no = noise (drums).
# Orders: list of pattern names or (name, transpose); "LOOP" marks the loop
# point (no LOOP = one-shot song that stops at the end).

# ---------------------------------------------------------------- waves ----
def _wave(fn):
    out = []
    for i in range(32):
        v = fn(i / 32.0)            # -1..1
        out.append(max(0, min(15, int(round(7.5 + 7.5 * v)))))
    return out

WAVES = [
    # warm saw: saw softened with a sine sub - fat synthwave bass
    ("saw", _wave(lambda t: 0.62 * (1.0 - 2.0 * t) + 0.38 * math.sin(2 * math.pi * t))),
    # pure-ish sine: soft sub bass / pads
    ("sine", _wave(lambda t: math.sin(2 * math.pi * t))),
    # hollow organ: odd harmonics, mysterious / reedy
    ("organ", _wave(lambda t: 0.62 * math.sin(2 * math.pi * t) + 0.28 * math.sin(6 * math.pi * t)
                    + 0.10 * math.sin(10 * math.pi * t))),
]

# ---------------------------------------------------------- instruments ----
# pulse: env (NRx2), duty 0..3 (12.5/25/50/75%), alt+rate = duty toggle macro
# wave : wave, level (1=100%, 2=50%, 3=25%), decay = frames per level step
# vshift/vrate/vdelay = vibrato, cut = auto note-off after N frames
INSTRUMENTS = [
    ("lead",     dict(env=0xB7, duty=2, alt=1, rate=6, vshift=7, vrate=22, vdelay=14)),
    ("lead2",    dict(env=0xA6, duty=1, vshift=7, vrate=24, vdelay=10)),
    ("leadsoft", dict(env=0x75, duty=1, vshift=8, vrate=20, vdelay=12)),
    ("pluck",    dict(env=0x71, duty=1)),
    ("pluck2",   dict(env=0x61, duty=2)),
    ("glass",    dict(env=0x62, duty=0)),
    ("bell",     dict(env=0x96, duty=0, alt=1, rate=3, vshift=8, vrate=14, vdelay=20)),
    ("bellsoft", dict(env=0x55, duty=0, vshift=8, vrate=12, vdelay=24)),
    ("music",    dict(env=0x43, duty=2)),
    ("chord",    dict(env=0x87, duty=2, alt=1, rate=4)),
    ("stab",     dict(env=0xA2, duty=1)),
    ("bass",     dict(kind="wave", wave="saw", level=1)),
    ("bassp",    dict(kind="wave", wave="saw", level=1, decay=6)),
    ("sub",      dict(kind="wave", wave="sine", level=1)),
    ("subsoft",  dict(kind="wave", wave="sine", level=2)),
    ("organ",    dict(kind="wave", wave="organ", level=1, decay=9)),
]

# ---------------------------------------------------------------- drums ----
# (grid char, NR42 envelope, [NR43 per frame, 1..4 values])
DRUMS = [
    ("K", 0xF1, [0x31, 0x55, 0x66, 0x77]),   # kick: click then falling rumble
    ("k", 0x91, [0x41, 0x56, 0x67]),         # soft kick
    ("S", 0xC2, [0x22, 0x13, 0x23, 0x33]),   # snare
    ("s", 0x51, [0x23]),                     # ghost snare
    ("h", 0x41, [0x00]),                     # closed hat
    ("H", 0x71, [0x00]),                     # accent hat
    ("o", 0x53, [0x01]),                     # open hat
    ("C", 0x96, [0x02, 0x03, 0x04]),         # crash / noise swell
    ("t", 0x51, [0x2C]),                     # rim tick (short 7-bit mode = metallic)
    ("z", 0x21, [0x03]),                     # shaker (very soft)
    ("g", 0x92, [0x1D, 0x0B, 0x3E, 0x0A]),   # glitch
]

# -------------------------------------------------------------- helpers ----
def roll(chords, pat, L=1):
    """Rolling arpeggio: for each chord (space separated notes) emit pat
    (sequence of chord-tone indices, None = hold)."""
    out = []
    for ch in chords:
        notes = ch.split()
        for i in pat:
            out.append(("%s:%d" % (notes[i], L)) if i is not None else "-:%d" % L)
    return " ".join(out)

def phase(chords, cycle, steps=16, start=0):
    """Arpeggio whose tone cycle (length != 16) keeps running across bars,
    so it phases against the bar line (Reich-style hypnosis)."""
    out = []
    k = start
    for ch in chords:
        notes = ch.split()
        for _ in range(steps):
            out.append("%s:1" % notes[cycle[k % len(cycle)]])
            k += 1
    return " ".join(out)

def rest_bars(n):
    return " ".join(["r:16"] * n)

def grid(*bars):
    return " ".join(bars)

# ================================================================= TITLE ===
# E minor, ~128 BPM.  i - VI - III - VII hook, B section with the harmonic
# minor dominant (B) pulling back home, 4-bar C64-chord breakdown.
EM, C, G, D = "e4 g4 b4 e5", "e4 g4 c5 e5", "d4 g4 b4 d5", "d4 f#4 a4 d5"
AM, BM = "e4 a4 c5 e5", "d#4 f#4 b4 d#5"
UPDN = [0, 1, 2, 3, 2, 1, 0, 1, 2, 3, 2, 1, 0, 1, 2, 3]

title = dict(
    speed=7,
    patterns=dict(
        rest4=rest_bars(4),
        hook="@lead L2 "
             "e5 - b4 e5 - g5 - f#5 | g5 - e5 - c5 - d5 e5 | "
             "d5 - - b4 - g4 a4 b4 | a4 - - - f#4 - a4 - | "
             "e5 - b4 e5 - g5 - a5 | b5 - g5 - e5 - g5 a5 | "
             "b5 - - d6 - b5 a5 g5 | f#5 - - - - - r r |",
        leadb="@lead2 L2 "
              "c6 - - - b5 - a5 - | g5 - - - e5 - g5 - | "
              "b5 - - - - - e5 f#5 | g5 - f#5 - d5 - - - | "
              "c6 - - - b5 - a5 - | g5 - - - e5 - c6 - | "
              "b5 - - - a5 - f#5 - | d#5 - - - f#5 - ~b5 - |",
        brk="@chord A37 e5:16 | A47 c5:16 | A47 g4:16 | A47 d5:8 A00 @lead2 f#5:4 ~a5:4 |",
        arpi="@pluck2 " + roll([EM, C, G, D], UPDN),
        arpa="@pluck " + roll([EM, C, G, D], UPDN),
        arpb="@pluck " + roll([AM, C, EM, D, AM, C, BM, BM], UPDN),
        arpbrk="@glass " + roll([EM, C, G, D], [3, 2, 1, 0, 3, 2, 1, 0, 3, 2, 1, 0, 3, 2, 1, 0]),
        bassi="@sub L8 e2 - | c2 - | g1 - | d2 - |",
        bassa="@bassp L2 " + " ".join("%s1 %s2 " % (n, n) * 4 for n in ["e", "c", "g", "d"]).replace("g1 g2", "g1 g2"),
        bassb="@bassp L2 " + " ".join("%s %s " % (lo, hi) * 4 for lo, hi in
                                     [("a1", "a2"), ("c2", "c3"), ("e1", "e2"), ("d2", "d3"),
                                      ("a1", "a2"), ("c2", "c3"), ("b1", "b2"), ("b1", "b2")]),
        bassbrk="@bass L16 e1 | c2 | g1 | d2 |",
        dri=grid("h.h.h.h.h.h.h.h.", "h.h.h.h.h.h.h.hh", "K.h.h.h.K.h.h.h.", "K.h.S.h.K.hKS.SS"),
        dra=grid("C.h.S.h.K.h.S.hh", "K.h.S.h.K.hKS.h.", "K.h.S.h.K.h.S.hh", "K.h.S.h.K.hKS.ss") + " " +
            grid("K.h.S.h.K.h.S.hh", "K.h.S.h.K.hKS.h.", "K.h.S.h.K.h.S.hh", "K.h.S.hSK.SKS.SS"),
        drb=grid("C.h.S.h.K.o.S.h.", "K.h.S.h.K.o.S.hh", "K.h.S.h.K.o.S.h.", "K.h.S.h.K.oKS.hh") + " " +
            grid("K.h.S.h.K.o.S.h.", "K.h.S.h.K.o.S.hh", "K.h.S.h.K.o.S.h.", "K.S.S.S.SSSSSSSS"),
        drbrk=grid("C...............", "k.......s.......", "k.......s.......", "k.......S.S.SSSS"),
    ),
    order=dict(
        p1=["rest4", "LOOP", "hook", "leadb", "brk"],
        p2=["arpi", "LOOP", "arpa", "arpa", "arpb", "arpbrk"],
        wv=["bassi", "LOOP", "bassa", "bassa", "bassb", "bassbrk"],
        no=["dri", "LOOP", "dra", "drb", "drbrk"],
    ),
)

# ================================================================= RUN_A ===
# A minor, ~112 BPM, pulsing.  Slow harmonic rhythm (2 bars per chord) so it
# stays out of the way while you think.  3-note arp cycle phases against the
# bar.  Lead enters in bar 5 with a question, section B answers, bridge lifts.
RA_AM, RA_F, RA_DM, RA_E = "a4 c5 e5", "a4 c5 f5", "a4 d5 f5", "g#4 b4 e5"
RA_G, RA_EM, RA_C = "g4 b4 d5", "g4 b4 e5", "g4 c5 e5"

def ra_bass(notes):
    out = []
    for lo in notes:
        hi = lo[:-1] + str(int(lo[-1]) + 1)
        out.append("%s:2 %s:1 %s:1 %s:2 %s:2 %s:2 %s:1 %s:1 %s:2 %s:2 |" % (lo, lo, hi, lo, hi, lo, lo, hi, lo, hi))
    return "@bassp " + " ".join(out)

run_a = dict(
    speed=8,
    patterns=dict(
        la="r:16 | r:16 | r:16 | r:8 @leadsoft e5:4 a5:4 | "
           "@lead2 a5:6 g5:2 f5:4 e5:4 | d5:12 r:2 d5:2 | e5:4 f5:2 g#5:6 b5:4 | g#5:12 r:4 |",
        lb="@lead2 e5:3 a5:3 b5:2 c6:4 b5:2 a5:2 | e5:8 r:4 c5:2 d5:2 | "
           "e5:3 a5:3 b5:2 c6:4 d6:2 c6:2 | a5:12 r:2 g5:2 | "
           "f5:3 a5:3 d6:2 c6:4 a5:2 f5:2 | a5:8 g5:4 f5:4 | "
           "e5:3 g#5:3 b5:2 d6:4 c6:2 b5:2 | g#5:12 r:4 |",
        lc="@lead c6:8 ~a5:8 | b5:8 d6:8 | b5:4 g5:4 e5:8 | a5:16 | "
           "c6:8 a5:8 | d6:8 b5:8 | g#5:8 b5:8 | e6:12 r:4 |",
        aa="@pluck2 " + phase([RA_AM, RA_AM, RA_F, RA_F, RA_DM, RA_DM, RA_E, RA_E], [0, 1, 2]),
        ab="@pluck " + phase([RA_AM, RA_AM, RA_F, RA_F, RA_DM, RA_DM, RA_E, RA_E], [0, 1, 2, 1, 2]),
        ac="@pluck " + roll([RA_F, RA_G, RA_EM, RA_AM, RA_F, RA_G, RA_E, RA_E],
                            [0, 1, 2, 1, 0, 1, 2, 1, 0, 1, 2, 1, 0, 1, 2, 1]),
        ba=ra_bass(["a1", "a1", "f1", "f1", "d2", "d2", "e1", "e1"]),
        bc=ra_bass(["f1", "g1", "e1", "a1", "f1", "g1", "e1", "e1"]),
        da=grid("K.h.h.K.S.h.h.h.", "K.h.h.K.S.h.h.h.", "K.h.h.K.S.h.h.h.", "K.h.h.K.S.h.K.hs") + " " +
           grid("K.h.h.K.S.h.h.h.", "K.h.h.K.S.h.h.h.", "K.h.h.K.S.h.h.h.", "K.h.h.K.S.hsK.SS"),
        db=grid("C.h.h.K.S.h.h.hh", "K.h.h.K.S.h.h.h.", "K.h.h.K.S.h.h.hh", "K.h.h.K.S.h.K.hs") + " " +
           grid("K.h.h.K.S.h.h.hh", "K.h.h.K.S.h.h.h.", "K.h.h.K.S.h.h.hh", "K.h.h.K.S.hsK.SS"),
        dc=grid("C.h.o.K.S.h.o.hh", "K.h.o.K.S.h.o.h.", "K.h.o.K.S.h.o.hh", "K.h.o.K.S.h.K.hs") + " " +
           grid("K.h.o.K.S.h.o.hh", "K.h.o.K.S.h.o.h.", "K.hsS.K.S.hsS.K.", "S.S.S.S.SSSSCCCC"),
    ),
    order=dict(
        p1=["LOOP", "la", "lb", "lc"],
        p2=["LOOP", "aa", "ab", "ac"],
        wv=["LOOP", "ba", "ba", "bc"],
        no=["LOOP", "da", "db", "dc"],
    ),
)

# ================================================================= RUN_B ===
# D dorian, ~100 BPM, mysterious.  Dm <-> G (the bright dorian 6th), a 6-step
# glassy arp phasing against the 16-step bar, 3+3+2 organ bass, rim ticks.
RB_DM, RB_G, RB_BB, RB_C = "d4 f4 a4 e5", "d4 g4 b4 e5", "d4 f4 bb4 c5", "e4 g4 c5 d5"
RB_A, RB_AM = "e4 a4 c#5 e5", "e4 a4 c5 e5"
RB_CYC = [0, 1, 2, 3, 1, 2]

def rb_bass(roots):
    out = []
    for r, f in roots:
        hi = r[:-1] + str(int(r[-1]) + 1)
        out.append("%s:3 %s:3 %s:2 %s:3 %s:3 %s:2 |" % (r, hi, f, r, hi, f))
    return "@organ " + " ".join(out)

run_b = dict(
    speed=9,
    patterns=dict(
        la="r:16 | r:16 | r:16 | r:16 | "
           "@bell f5:6 e5:2 d5:8 | e5:6 g5:2 c6:8 | a5:16 | r:8 @bellsoft e5:4 a5:4 |",
        lb="@bell a5:4 b5:2 c6:2 d6:8 | b5:6 a5:2 g5:8 | f5:4 g5:2 a5:2 c6:4 a5:4 | b5:12 r:4 | "
           "d6:6 c6:2 bb5:4 a5:4 | g5:6 e5:2 c5:4 e5:4 | d5:16 | c#5:6 e5:2 a5:8 |",
        lc="@bellsoft d6:8 ~f6:8 | e6:8 ~g6:8 | e6:6 c#6:2 a5:8 | @lead2 ~e6:8 r:8 |",
        aa="@glass " + phase([RB_DM, RB_G, RB_DM, RB_G, RB_BB, RB_C, RB_DM, RB_AM], RB_CYC),
        ab="@glass " + phase([RB_DM, RB_G, RB_DM, RB_G, RB_BB, RB_C, RB_DM, RB_A], RB_CYC, start=3),
        ac="@pluck2 " + phase([RB_BB, RB_C, RB_A, RB_A], [0, 1, 2, 3, 2, 1, 0, 2]),
        ba=rb_bass([("d2", "a2"), ("g1", "d2"), ("d2", "a2"), ("g1", "d2"),
                    ("bb1", "f2"), ("c2", "g2"), ("d2", "a2"), ("a1", "e2")]),
        bc=rb_bass([("bb1", "f2"), ("c2", "g2"), ("a1", "e2"), ("a1", "e2")]),
        da=grid("k...z.t.z...z.t.", "k...z.t.z.k.z.t.", "k...z.t.z...z.t.", "k...z.t.z.k.z.tt") + " " +
           grid("k...z.t.S...z.t.", "k...z.t.S.k.z.t.", "k...z.t.S...z.t.", "k...z.t.S.k.sstt"),
        db=grid("K.z.z.t.S.z.z.t.", "K.z.z.t.S.zKz.t.", "K.z.z.t.S.z.z.t.", "K.z.z.t.S.zKz.tt") + " " +
           grid("K.z.z.t.S.z.z.t.", "K.z.z.t.S.zKz.t.", "K.z.z.t.S.z.z.t.", "K.z.z.t.S.sKsSSS"),
        dc=grid("C.......k.......", "k.......k.......", "k...t...k...t...", "k.t.t.t.S.t.SsSS"),
    ),
    order=dict(
        p1=["LOOP", "la", "lb", "lc"],
        p2=["LOOP", "aa", "ab", "ac"],
        wv=["LOOP", "ba", "ba", "bc"],
        no=["LOOP", "da", "db", "dc"],
    ),
)

# =================================================================== ZEN ===
# F lydian / C major, ~90 BPM, sparse.  Music-box arps, sine sub, soft bell.
ZF, ZG, ZE, ZA = "f4 a4 c5 e5", "f4 g4 b4 d5", "e4 g4 b4 d5", "e4 g4 a4 c5"
ZD, ZG2, ZC = "d4 f4 a4 c5", "d4 g4 b4 d5", "e4 g4 b4 c5"
ZPAT = [0, None, 1, None, 2, None, 3, None, 2, None, 1, None, 3, None, 2, None]

zen = dict(
    speed=10,
    patterns=dict(
        l1="@bell a5:8 c6:4 e6:4 | d6:12 b5:4 | g5:8 b5:4 e6:4 | c6:16 | "
           "f5:8 a5:4 c6:4 | b5:8 d6:4 g6:4 | e6:8 d6:4 b5:4 | c6:16 |",
        l2="@bellsoft r:8 c7:4 b6:4 | a6:16 | r:8 g6:4 e6:4 | a6:16 | "
           "r:16 | r:8 b6:4 d7:4 | e7:12 d7:4 | c7:16 |",
        a1="@music " + roll([ZF, ZG, ZE, ZA, ZD, ZG2, ZC, ZC], ZPAT),
        a2="@music " + roll([ZF, ZG, ZE, ZA, ZD, ZG2, ZC, ZC],
                            [3, None, 2, None, 1, None, 0, None, 1, None, 2, None, 0, None, 1, None]),
        b1="@subsoft L16 f2 | f2 | e2 | a1 | d2 | g1 | c2 | c2 |",
        b2="@sub L8 f2 c2 | f2 g1 | e2 b1 | a1 e2 | d2 a1 | g1 d2 | c2 g1 | c2 - |",
        d1=grid("z...............", "................", "........t.......", "................",
                "................", "........t.......", "................", "....t.......t...") ,
        d2=grid("z.....t.z.......", "z.....t.z...t...", "z.....t.z.......", "z.....t.z...t.t.",
                "z.....t.z.......", "z.....t.z...t...", "z.....t.z.......", "z.....t.z.t.t.t."),
    ),
    order=dict(
        p1=["LOOP", "l1", "l2"],
        p2=["LOOP", "a1", "a2"],
        wv=["LOOP", "b1", "b2"],
        no=["LOOP", "d1", "d2"],
    ),
)

# ============================================================== GAMEOVER ===
# ~4 s, chromatic "corrupting" descent onto A minor, then stops.
gameover = dict(
    speed=6,
    patterns=dict(
        l="@lead e5:3 d#5:3 d5:3 c#5:3 c5:4 b4:4 bb4:4 ~a4:16",
        h="@stab c5:6 b4:6 a4:4 g#4:8 @leadsoft A37 a4:16",
        b="@bass a2:8 g2:8 f2:8 e2:4 a1:12",
        d="C.......g.......g.......K...............",
    ),
    order=dict(p1=["l"], p2=["h"], wv=["b"], no=["d"]),
)

SONGS = dict(TITLE=title, RUN_A=run_a, RUN_B=run_b, ZEN=zen, GAMEOVER=gameover)
