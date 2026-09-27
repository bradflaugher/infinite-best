#!/usr/bin/env python3
"""gen_music.py - INFINITE BEST music/sfx compiler (stdlib only).

Reads the song + sfx sources in assets/music/ (plain Python files using the
tiny tracker-style notation described below) and emits src/gb/music_data.c
(+ src/gb/music_data.h) for the driver in src/gb/sound.c.

    python3 tools/gen_music.py            # writes src/gb/music_data.[ch]
    python3 tools/gen_music.py --stats    # also prints sizes / durations

---------------------------------------------------------------------------
Pattern notation (pulse / wave channels) - whitespace separated tokens,
every event token lasts the current default length L (in rows) unless it has
an explicit ':rows' suffix.  A row is a 16th note; one bar = 16 rows.

    e5  f#4  bb3      note (absolute pitch; C4 = middle C). '#' sharp, 'b' flat
    e5:3              note lasting 3 rows
    r   r:8           rest (note off)
    -   -:4           hold / extend the previous note or rest
    L2                default length = 2 rows (8th notes)
    @lead             select instrument by name
    A37  A00          arpeggio: cycle note, +3, +7 semitones per frame (A00=off,
                      A0c = 2-step octave arp)
    ~e5               glide (portamento, legato) from previous note into e5
    |                 bar line: asserts position is a multiple of 16 rows

Drum patterns (noise channel) are grids, one char per row:
    K kick, k soft kick, S snare, s ghost snare, h hat, H accent hat,
    o open hat, C crash, t rim tick, z shaker, g glitch, '.' or '-' = hold.
    Spaces and '|' are ignored.
---------------------------------------------------------------------------
"""
import math
import os
import runpy
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, "assets", "music")
OUT_C = os.path.join(ROOT, "src", "gb", "music_data.c")
OUT_H = os.path.join(ROOT, "src", "gb", "music_data.h")

SONG_ORDER = ["TITLE", "RUN_A", "RUN_B", "ZEN", "GAMEOVER"]
SFX_ORDER = ["MOVE", "BUMP", "CHIP", "SWITCH", "PORTAL", "ARROW", "PIT",
             "REWIND", "MENU", "SELECT", "WIN", "BEST", "UNLOCK", "GEN",
             "NEWMECH", "HINT", "ERROR"]

# ---- stream byte codes (must match sound.c) -------------------------------
CMD_LEN = 0x80      # 0x80..0xBF : len = (b & 0x3F) + 1
CMD_REST = 0xC0
CMD_HOLD = 0xC1
CMD_INST = 0xC2     # + instrument index
CMD_ARP = 0xC3      # + xy
CMD_GLIDE = 0xC4    # next note glides
CMD_END = 0xFF
ORD_JUMP = 0xFF     # + byte offset
ORD_END = 0xFE
NUM_NOTES = 96      # frequency table: index 0 = C2 on pulse (C1 on wave)
MAX_LEN = 64

NOTE_BASE = {"c": 0, "d": 2, "e": 4, "f": 5, "g": 7, "a": 9, "b": 11}


class MusicError(Exception):
    pass


def midi_of(tok):
    """'f#4' -> midi number.  Returns None if tok is not a note."""
    if not tok or tok[0] not in NOTE_BASE:
        return None
    i = 1
    semi = NOTE_BASE[tok[0]]
    if i < len(tok) and tok[i] == "#":
        semi += 1
        i += 1
    elif i + 1 < len(tok) and tok[i] == "b" and (tok[i + 1].isdigit() or tok[i + 1] == "-"):
        semi -= 1
        i += 1
    rest = tok[i:]
    try:
        octv = int(rest)
    except ValueError:
        return None
    return (octv + 1) * 12 + semi


def hz_of_midi(m):
    return 440.0 * (2.0 ** ((m - 69) / 12.0))


def reg_of_hz(hz):
    v = int(round(2048 - 131072.0 / hz))
    return max(0, min(2047, v))


# ---------------------------------------------------------------------------
# Pattern compilation
# ---------------------------------------------------------------------------
def parse_events(src, kind, inst_index, name):
    """Returns list of events: ('note', idx, rows, pre_cmds) / ('rest', rows) etc.
    kind: 'pulse' | 'wave'."""
    base = 36 if kind == "pulse" else 24
    L = 1
    pos = 0
    events = []          # [type, value, rows, precmds]
    pending = []         # commands to emit before next event
    glide = False
    for raw in src.split():
        tok = raw
        if tok == "|":
            if pos % 16:
                raise MusicError("%s: bar line at row %d (not multiple of 16)" % (name, pos))
            continue
        if tok[0] == "L" and tok[1:].isdigit():
            L = int(tok[1:])
            continue
        if tok[0] == "@":
            iname = tok[1:]
            if iname not in inst_index:
                raise MusicError("%s: unknown instrument %s" % (name, iname))
            pending.append((CMD_INST, inst_index[iname]))
            continue
        if tok[0] == "A" and len(tok) == 3:
            pending.append((CMD_ARP, int(tok[1:], 16)))
            continue
        rows = L
        if ":" in tok:
            tok, r = tok.split(":")
            rows = int(r)
        if rows <= 0:
            raise MusicError("%s: bad length in %s" % (name, raw))
        if tok.startswith("~"):
            glide = True
            tok = tok[1:]
        if tok == "-":
            if not events:
                raise MusicError("%s: hold at start of pattern" % name)
            events[-1][2] += rows
        elif tok == "r":
            events.append(["rest", 0, rows, pending])
            pending = []
        else:
            m = midi_of(tok)
            if m is None:
                raise MusicError("%s: bad token %r" % (name, raw))
            idx = m - base
            if not 0 <= idx < NUM_NOTES:
                raise MusicError("%s: note %s out of range for %s" % (name, raw, kind))
            if glide:
                pending.append((CMD_GLIDE,))
                glide = False
            events.append(["note", idx, rows, pending])
            pending = []
        pos += rows
    if pending:
        raise MusicError("%s: trailing commands with no note" % name)
    return events, pos


DRUM_CHARS = "KkSshHoCtzg"


def parse_drums(src, drum_index, name):
    events = []
    pos = 0
    for ch in src:
        if ch in " \t\n|":
            continue
        if ch in ".-":
            if not events:
                events.append(["rest", 0, 1, []])
            else:
                events[-1][2] += 1
        elif ch in drum_index:
            events.append(["note", drum_index[ch], 1, []])
        else:
            raise MusicError("%s: bad drum char %r" % (name, ch))
        pos += 1
    return events, pos


def encode_events(events):
    out = []
    cur_len = None

    def set_len(n):
        nonlocal cur_len
        if cur_len != n:
            out.append(CMD_LEN | (n - 1))
            cur_len = n

    for typ, val, rows, pre in events:
        for c in pre:
            out.extend(c)
        first = True
        while rows > 0:
            n = min(rows, MAX_LEN)
            set_len(n)
            if first:
                out.append(val if typ == "note" else CMD_REST)
                first = False
            else:
                out.append(CMD_HOLD)
            rows -= n
    out.append(CMD_END)
    return out


# ---------------------------------------------------------------------------
# SFX helpers (exposed to assets/music/sfx.py)
# ---------------------------------------------------------------------------
NR = {"NR10": 0x10, "NR11": 0x11, "NR12": 0x12, "NR13": 0x13, "NR14": 0x14,
      "NR21": 0x16, "NR22": 0x17, "NR23": 0x18, "NR24": 0x19,
      "NR30": 0x1A, "NR31": 0x1B, "NR32": 0x1C, "NR33": 0x1D, "NR34": 0x1E,
      "NR41": 0x20, "NR42": 0x21, "NR43": 0x22, "NR44": 0x23}


def _freq(note=None, hz=None, reg=None):
    if reg is not None:
        return reg
    if hz is None:
        m = midi_of(note) if isinstance(note, str) else note
        hz = hz_of_midi(m)
    return reg_of_hz(hz)


def P1(note=None, duty=2, env=0xF1, sweep=0x00, hz=None, reg=None):
    f = _freq(note, hz, reg)
    return [(0x10, sweep), (0x11, duty << 6), (0x12, env), (0x13, f & 0xFF), (0x14, 0x80 | (f >> 8))]


def P2(note=None, duty=2, env=0xF1, hz=None, reg=None):
    f = _freq(note, hz, reg)
    return [(0x16, duty << 6), (0x17, env), (0x18, f & 0xFF), (0x19, 0x80 | (f >> 8))]


def P1F(note=None, hz=None, reg=None):
    f = _freq(note, hz, reg)
    return [(0x13, f & 0xFF), (0x14, f >> 8)]


def P2F(note=None, hz=None, reg=None):
    f = _freq(note, hz, reg)
    return [(0x18, f & 0xFF), (0x19, f >> 8)]


def P1D(duty):
    return [(0x11, duty << 6)]


def P2D(duty):
    return [(0x16, duty << 6)]


def NOI(poly, env=0xF1):
    return [(0x21, env), (0x22, poly), (0x23, 0x80)]


def NOIP(poly):
    return [(0x22, poly)]


def OFF1():
    return [(0x12, 0x00)]


def OFF2():
    return [(0x17, 0x00)]


def OFF4():
    return [(0x21, 0x00)]


class Timeline:
    def __init__(self):
        self.ev = {}
        self.end = 0

    def at(self, frame, writes):
        self.ev.setdefault(frame, []).extend(writes)
        self.end = max(self.end, frame + 1)
        return self

    def length(self, n):
        self.end = max(self.end, n)
        return self


REG_CH = {}
for _r in range(0x10, 0x15):
    REG_CH[_r] = 0
for _r in range(0x16, 0x1A):
    REG_CH[_r] = 1
for _r in list(range(0x1A, 0x1F)) + list(range(0x30, 0x40)):
    REG_CH[_r] = 2
for _r in range(0x20, 0x24):
    REG_CH[_r] = 3


def compile_sfx(name, tl, mask, prio):
    out = []
    f = 0
    total = tl.end
    while f < total:
        for (reg, val) in tl.ev.get(f, []):
            if reg not in REG_CH:
                raise MusicError("sfx %s: bad reg %02x" % (name, reg))
            if not (mask >> REG_CH[reg]) & 1:
                raise MusicError("sfx %s: reg %02x outside channel mask" % (name, reg))
            if reg in (0x14, 0x19, 0x1E, 0x23) and val & 0x38:
                raise MusicError("sfx %s: bad NRx4 value %02x" % (name, val))
            out += [reg, val & 0xFF]
        # count idle frames that follow
        nxt = f + 1
        while nxt < total and nxt not in tl.ev:
            nxt += 1
        idle = nxt - f - 1
        k = min(idle, 63)
        out.append(0x40 + k)
        idle -= k
        while idle > 0:          # an empty frame, then up to 63 more idle
            idle -= 1
            k = min(idle, 63)
            out.append(0x40 + k)
            idle -= k
        f = nxt
    out.append(0x00)
    return out


# ---------------------------------------------------------------------------
# Main build
# ---------------------------------------------------------------------------
def c_bytes(data, per_line=16, indent="    "):
    lines = []
    for i in range(0, len(data), per_line):
        lines.append(indent + ",".join("0x%02X" % b for b in data[i:i + per_line]) + ",")
    return "\n".join(lines)


def build(stats=False):
    helpers = dict(P1=P1, P2=P2, P1F=P1F, P2F=P2F, P1D=P1D, P2D=P2D, NOI=NOI,
                   NOIP=NOIP, OFF1=OFF1, OFF2=OFF2, OFF4=OFF4, Timeline=Timeline,
                   midi_of=midi_of, hz_of_midi=hz_of_midi, reg_of_hz=reg_of_hz, math=math)
    songsrc = runpy.run_path(os.path.join(ASSETS, "songs.py"), init_globals=helpers)
    sfxsrc = runpy.run_path(os.path.join(ASSETS, "sfx.py"), init_globals=helpers)

    INSTS = songsrc["INSTRUMENTS"]   # list of (name, dict)
    WAVES = songsrc["WAVES"]         # list of (name, [32 samples 0..15])
    DRUMS = songsrc["DRUMS"]         # list of (char, env, [nr43...])
    SONGS = songsrc["SONGS"]         # dict name -> song dict
    SFX = sfxsrc["SFX"]              # dict name -> (Timeline, mask, prio)

    wave_index = {n: i for i, (n, _) in enumerate(WAVES)}
    inst_index = {}
    inst_rows = []
    inst_kind = {}
    for i, (n, d) in enumerate(INSTS):
        inst_index[n] = i
        kind = d.get("kind", "pulse")
        inst_kind[n] = kind
        if kind == "wave":
            row = [d.get("level", 1), wave_index[d["wave"]], 0, d.get("decay", 0),
                   d.get("vshift", 0), d.get("vrate", 0), d.get("vdelay", 0), d.get("cut", 0)]
        else:
            row = [d["env"], d.get("duty", 2), d.get("alt", d.get("duty", 2)), d.get("rate", 0),
                   d.get("vshift", 0), d.get("vrate", 0), d.get("vdelay", 0), d.get("cut", 0)]
        if row[4] and not 6 <= row[4] <= 11:
            raise MusicError("instrument %s: vshift must be 0 or 6..11" % n)
        inst_rows.append((n, row))
    drum_index = {}
    drum_rows = []
    for i, (ch, env, seq) in enumerate(DRUMS):
        drum_index[ch] = i
        if not 1 <= len(seq) <= 4:
            raise MusicError("drum %s seq length" % ch)
        drum_rows.append((ch, [env, len(seq)] + list(seq) + [0] * (4 - len(seq))))

    pat_bytes = []      # list of (cname, bytes)
    pat_cache = {}
    CHKIND = ["pulse", "pulse", "wave", "noise"]

    def get_pattern(song, pname, ch):
        kind = CHKIND[ch]
        key = (song["_name"], pname, kind)
        if key in pat_cache:
            return pat_cache[key]
        src = song["patterns"][pname]
        if kind == "noise":
            ev, rows = parse_drums(src, drum_index, pname)
        else:
            ev, rows = parse_events(src, kind, inst_index, "%s.%s" % (song["_name"], pname))
            for e in ev:
                for c in e[3]:
                    if c[0] == CMD_INST:
                        iname = INSTS[c[1]][0]
                        ik = inst_kind[iname]
                        if (ik == "wave") != (kind == "wave"):
                            raise MusicError("%s.%s: instrument %s used on %s channel"
                                             % (song["_name"], pname, iname, kind))
        if rows <= 0:
            raise MusicError("%s.%s: empty pattern" % (song["_name"], pname))
        data = encode_events(ev)
        idx = len(pat_bytes)
        if idx >= 0xFE:
            raise MusicError("too many patterns")
        pat_bytes.append(("pat_%s_%s_%s" % (song["_name"].lower(), pname, kind[0]), data))
        pat_cache[key] = (idx, rows)
        return pat_cache[key]

    song_out = []
    info = []
    for sname in SONG_ORDER:
        song = SONGS[sname]
        song["_name"] = sname
        speeds = song["speed"] if isinstance(song["speed"], tuple) else (song["speed"], song["speed"])
        ords = []
        lens = []
        for ch, key in enumerate(["p1", "p2", "wv", "no"]):
            order = song["order"][key]
            data = []
            pre = 0
            loop_rows = 0
            loop_at = None
            for ent in order:
                if ent == "LOOP":
                    loop_at = len(data)
                    continue
                if isinstance(ent, tuple):
                    pname, tr = ent
                else:
                    pname, tr = ent, 0
                idx, rows = get_pattern(song, pname, ch)
                data += [idx, tr & 0xFF]
                if loop_at is None:
                    pre += rows
                else:
                    loop_rows += rows
            if loop_at is not None:
                data += [ORD_JUMP, loop_at]
            else:
                data += [ORD_END, 0]
            if len(data) > 255:
                raise MusicError("order too long")
            ords.append(data)
            lens.append((pre, loop_rows, loop_at is not None))
        if len(set(lens)) != 1:
            raise MusicError("song %s: channel lengths differ: %r" % (sname, lens))
        pre, loop_rows, looping = lens[0]
        avg = (speeds[0] + speeds[1]) / 2.0
        info.append((sname, pre, loop_rows, looping, avg))
        song_out.append((sname, speeds, ords))

    sfx_out = []
    for n in SFX_ORDER:
        tl, mask, prio = SFX[n]
        sfx_out.append((n, compile_sfx(n, tl, mask, prio), mask, prio, tl.end))

    # ---- frequency table ----
    freqs = [reg_of_hz(hz_of_midi(36 + i)) for i in range(NUM_NOTES)]

    # ---- wavetables ----
    wave_bytes = []
    for n, s in WAVES:
        if len(s) != 32 or min(s) < 0 or max(s) > 15:
            raise MusicError("wave %s bad" % n)
        wave_bytes.append((n, [(s[i] << 4) | s[i + 1] for i in range(0, 32, 2)]))

    # ------------------------------------------------------------------
    h = []
    h.append("/* music_data.h - GENERATED by tools/gen_music.py, do not edit. */")
    h.append("#ifndef MUSIC_DATA_H\n#define MUSIC_DATA_H\n\n#include <stdint.h>\n")
    h.append("/* pattern stream byte codes */")
    h.append("#define SND_CMD_LEN   0x%02X /* 0x80..0xBF: len=(b&0x3F)+1 rows */" % CMD_LEN)
    h.append("#define SND_CMD_REST  0x%02X" % CMD_REST)
    h.append("#define SND_CMD_HOLD  0x%02X" % CMD_HOLD)
    h.append("#define SND_CMD_INST  0x%02X /* + instrument */" % CMD_INST)
    h.append("#define SND_CMD_ARP   0x%02X /* + xy */" % CMD_ARP)
    h.append("#define SND_CMD_GLIDE 0x%02X" % CMD_GLIDE)
    h.append("#define SND_CMD_END   0x%02X" % CMD_END)
    h.append("#define SND_ORD_JUMP  0x%02X /* + byte offset into order list */" % ORD_JUMP)
    h.append("#define SND_ORD_END   0x%02X" % ORD_END)
    h.append("#define SND_NUM_NOTES %d" % NUM_NOTES)
    h.append("#define SND_NUM_INSTS %d" % len(inst_rows))
    h.append("#define SND_NUM_DRUMS %d" % len(drum_rows))
    h.append("#define SND_NUM_WAVES %d" % len(wave_bytes))
    h.append("#define SND_NUM_PATTERNS %d" % len(pat_bytes))
    h.append("")
    h.append("""/* Instrument (8 bytes, ROM).
 *   pulse: env = NRx2 envelope, duty/alt = duty 0..3, rate = frames between
 *          duty toggles (0 = no duty macro)
 *   wave : env = start output level code 1..3 (100/50/25%), duty = wavetable
 *          index, rate = frames per level-decay step (0 = sustain)
 *   all  : vshift = vibrato depth (period >> vshift, 0 = off), vrate = LFO
 *          phase step per frame (256 = full cycle), vdelay = frames before
 *          vibrato, cut = frames until auto note-off (0 = none) */
typedef struct {
    uint8_t env, duty, alt, rate, vshift, vrate, vdelay, cut;
} snd_inst_t;

/* Noise drum: NR42 envelope + up to 4 per-frame NR43 values. */
typedef struct {
    uint8_t env, len, seq[4];
} snd_drum_t;

typedef struct {
    uint8_t speed_a, speed_b;       /* frames per row, alternating (groove) */
    const uint8_t *ord[4];          /* order lists: (pattern, transpose)* */
} snd_song_t;

/* SFX stream: [reg(0x10..0x3F), value]* ... 0x40+n = end of frame, then n
 * idle frames ... 0x00 = end.  mask: bit0 pulse1, bit1 pulse2, bit2 wave,
 * bit3 noise. */
typedef struct {
    const uint8_t *data;
    uint8_t mask, prio;
} snd_sfx_t;
""")
    h.append("extern const uint16_t snd_freq[SND_NUM_NOTES];")
    h.append("extern const snd_inst_t snd_insts[SND_NUM_INSTS];")
    h.append("extern const snd_drum_t snd_drums[SND_NUM_DRUMS];")
    h.append("extern const uint8_t snd_waves[SND_NUM_WAVES][16];")
    h.append("extern const uint8_t * const snd_patterns[SND_NUM_PATTERNS];")
    h.append("extern const snd_song_t snd_songs[%d];" % len(song_out))
    h.append("extern const snd_sfx_t snd_sfx[%d];" % len(sfx_out))
    h.append("/* approximate lengths, for tests / tools */")
    h.append("extern const uint16_t snd_song_frames[%d]; /* intro+loop body in frames */" % len(song_out))
    h.append("extern const uint8_t snd_song_loops[%d];   /* 1 = looping */" % len(song_out))
    h.append("extern const uint8_t snd_sfx_frames[%d];   /* sfx duration in frames */" % len(sfx_out))
    h.append("\n#endif")

    c = []
    c.append("/* music_data.c - GENERATED by tools/gen_music.py from assets/music/songs.py + sfx.py.")
    c.append(" * Do not edit by hand; edit the sources and re-run the tool. */")
    c.append("#ifndef HOST_TEST")
    c.append("#pragma bank 255")
    c.append("#include <gb/gb.h>")
    c.append("BANKREF(music_data)")
    c.append("#endif")
    c.append('#include "music_data.h"\n')
    c.append("const uint16_t snd_freq[SND_NUM_NOTES] = {")
    for i in range(0, NUM_NOTES, 12):
        c.append("    " + ",".join("%4d" % f for f in freqs[i:i + 12]) + ", /* C%d */" % (2 + i // 12))
    c.append("};\n")
    c.append("const snd_inst_t snd_insts[SND_NUM_INSTS] = {")
    for n, row in inst_rows:
        c.append("    {" + ",".join("0x%02X" % v for v in row) + "}, /* %s */" % n)
    c.append("};\n")
    c.append("const snd_drum_t snd_drums[SND_NUM_DRUMS] = {")
    for n, row in drum_rows:
        c.append("    {0x%02X,%d,{0x%02X,0x%02X,0x%02X,0x%02X}}, /* %s */" % tuple(row + [n]))
    c.append("};\n")
    c.append("const uint8_t snd_waves[SND_NUM_WAVES][16] = {")
    for n, b in wave_bytes:
        c.append("    {" + ",".join("0x%02X" % v for v in b) + "}, /* %s */" % n)
    c.append("};\n")
    for n, d in pat_bytes:
        c.append("static const uint8_t %s[] = {\n%s\n};" % (n, c_bytes(d)))
    c.append("\nconst uint8_t * const snd_patterns[SND_NUM_PATTERNS] = {")
    for n, _ in pat_bytes:
        c.append("    %s," % n)
    c.append("};\n")
    for sname, speeds, ords in song_out:
        for ch, o in enumerate(ords):
            c.append("static const uint8_t ord_%s_%d[] = {%s};" % (sname.lower(), ch, ",".join("0x%02X" % v for v in o)))
    c.append("\nconst snd_song_t snd_songs[%d] = {" % len(song_out))
    for sname, speeds, ords in song_out:
        c.append("    {%d,%d,{ord_%s_0,ord_%s_1,ord_%s_2,ord_%s_3}}, /* %s */" % (
            speeds[0], speeds[1], sname.lower(), sname.lower(), sname.lower(), sname.lower(), sname))
    c.append("};\n")
    for n, d, mask, prio, fr in sfx_out:
        c.append("static const uint8_t sfx_%s[] = {\n%s\n};" % (n.lower(), c_bytes(d)))
    c.append("\nconst snd_sfx_t snd_sfx[%d] = {" % len(sfx_out))
    for n, d, mask, prio, fr in sfx_out:
        c.append("    {sfx_%s,0x%02X,%d}, /* %s: %d frames */" % (n.lower(), mask, prio, n, fr))
    c.append("};\n")
    c.append("const uint16_t snd_song_frames[%d] = {%s};" % (
        len(info), ",".join(str(int((p + l) * a)) for (_, p, l, lp, a) in info)))
    c.append("const uint8_t snd_song_loops[%d] = {%s};" % (len(info), ",".join("1" if lp else "0" for (_, p, l, lp, a) in info)))
    c.append("const uint8_t snd_sfx_frames[%d] = {%s};" % (len(sfx_out), ",".join(str(min(255, fr)) for (_, _, _, _, fr) in sfx_out)))

    with open(OUT_H, "w") as f:
        f.write("\n".join(h) + "\n")
    with open(OUT_C, "w") as f:
        f.write("\n".join(c) + "\n")

    if stats:
        total = sum(len(d) for _, d in pat_bytes)
        print("patterns: %d (%d bytes)" % (len(pat_bytes), total))
        print("sfx bytes: %d" % sum(len(d) for _, d, _, _, _ in sfx_out))
        for (sname, p, l, lp, a) in info:
            print("%-9s intro %3d rows, loop %3d rows, speed %.1f -> intro %.1fs, loop %.1fs %s"
                  % (sname, p, l, a, p * a / 60.0, l * a / 60.0, "(loops)" if lp else "(one-shot)"))
        for n, d, mask, prio, fr in sfx_out:
            print("sfx %-8s mask %x prio %d %3d frames (%.2fs) %d bytes" % (n, mask, prio, fr, fr / 60.0, len(d)))


if __name__ == "__main__":
    try:
        build(stats="--stats" in sys.argv)
    except MusicError as e:
        print("gen_music: error:", e, file=sys.stderr)
        sys.exit(1)
