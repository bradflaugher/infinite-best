#ifdef __SDCC
#pragma bank 255
#endif
#include <string.h>
#include "solver.h"

#define NSTATE_IDS 2048
#define QUEUE_MAX (LN * 16)

static uint8_t visited[NSTATE_IDS / 8];
static uint16_t queue[QUEUE_MAX];
uint16_t solve_visited;

static const uint8_t bitmask[8] = { 1, 2, 4, 8, 16, 32, 64, 128 };

/* Solver-only slide. Semantically identical to sim_move(..., NULL) but works on
 * file-scope statics and a private copy of the grid. On the Game Boy this is the
 * hottest loop in the game, so it is hand-written SM83 assembly (~10x faster than
 * SDCC output); the C version is the reference used on the host. Equivalence is
 * checked by the ROM self-test (tests/rom) which compares on-device par values
 * against the host build. */
uint8_t f_grid[GN];
uint8_t f_full, f_portal0, f_portal1;
uint8_t f_pos, f_chips, f_sw;
static const uint8_t chip_bit[3] = { 1, 2, 4 };

static void fast_setup(const Level *L)
{
    memcpy(f_grid, L->cell, GN);
    f_full = level_all_chips(L);
    f_portal0 = L->portal[0];
    f_portal1 = L->portal[1];
}

#if defined(__SDCC) && !defined(IB_NO_ASM)
/* sdcccall(1): dir in A, result in A. Register use:
 *   B = pos, C = delta, D = the gate type that is currently solid, E = steps */
static uint8_t fast_move(uint8_t dir) __naked
{
    (void)dir;
    __asm
        ld  e, a
        ld  d, #0
        ld  hl, #_dir_dpos
        add hl, de
        ld  c, (hl)
        ld  a, (_f_pos)
        ld  b, a
        ld  d, #12              ; T_GATE_A solid when sw == 0
        ld  a, (_f_sw)
        or  a
        jr  z, 00001$
        ld  d, #13              ; T_GATE_B solid when sw == 1
00001$:
        ld  e, #0
00010$:                          ; slide loop
        ld  a, b
        add a, c
        add a, #<(_f_grid)
        ld  l, a
        ld  a, #>(_f_grid)
        adc a, #0
        ld  h, a
        ld  a, (hl)             ; t = grid[pos + delta]
        cp  #1                  ; T_WALL
        jp  z, 00090$
        cp  d
        jp  z, 00090$
        ld  h, a
        ld  a, b
        add a, c
        ld  b, a                ; pos += delta
        inc e
        ld  a, h
        or  a
        jr  z, 00020$           ; T_FLOOR
        cp  #15                 ; T_PIT
        jr  z, 00080$
        cp  #6                  ; T_STOP
        jr  z, 00085$
        cp  #2                  ; T_EXIT
        jr  z, 00050$
        cp  #11                 ; T_SWITCH
        jr  z, 00040$
        cp  #14                 ; T_PORTAL
        jr  z, 00060$
        cp  #7                  ; T_CHIP..T_CHIP2 are 3..5
        jr  c, 00030$
        cp  #11                 ; arrows 7..10
        jr  c, 00070$
00020$:
        ld  a, e
        cp  #48                 ; SLIDE_MAX
        jr  c, 00010$
        ld  a, #4               ; MV_LOOP
        ret
00030$:                          ; chip
        sub a, #3
        ld  h, #1
        jr  z, 00031$
        ld  h, #2
        dec a
        jr  z, 00031$
        ld  h, #4
00031$:
        ld  a, (_f_chips)
        or  a, h
        ld  (_f_chips), a
        jr  00020$
00040$:                          ; switch
        ld  a, (_f_sw)
        xor a, #1
        ld  (_f_sw), a
        ld  a, d
        xor a, #1               ; 12 <-> 13
        ld  d, a
        jr  00020$
00050$:                          ; exit
        ld  a, (_f_full)
        ld  h, a
        ld  a, (_f_chips)
        cp  h
        jr  nz, 00020$
        ld  a, b
        ld  (_f_pos), a
        ld  a, #2               ; MV_WIN
        ret
00060$:                          ; portal
        ld  a, (_f_portal0)
        cp  b
        jr  nz, 00061$
        ld  a, (_f_portal1)
00061$:
        ld  b, a
        jr  00020$
00070$:                          ; arrow: delta = dir_dpos[t - 7]
        sub a, #7
        add a, #<(_dir_dpos)
        ld  l, a
        ld  a, #>(_dir_dpos)
        adc a, #0
        ld  h, a
        ld  c, (hl)
        jr  00020$
00080$:
        ld  a, #3               ; MV_DEAD
        ret
00085$:
        ld  a, b                ; stop pad
        ld  (_f_pos), a
        ld  a, #1               ; MV_OK
        ret
00090$:                          ; blocked
        ld  a, e
        or  a
        ret z                   ; MV_NONE (0)
        ld  a, b
        ld  (_f_pos), a
        ld  a, #1               ; MV_OK
        ret
    __endasm;
}
#else
static uint8_t fast_move(uint8_t dir)
{
    int8_t delta = dir_dpos[dir];
    uint8_t gate = f_sw ? T_GATE_B : T_GATE_A;
    uint8_t steps = 0, pos = f_pos, t;
    for (;;) {
        t = f_grid[(uint8_t)(pos + delta)];
        if (t == T_WALL || t == gate) break;
        pos = (uint8_t)(pos + delta);
        steps++;
        switch (t) {
        case T_PIT: return MV_DEAD;
        case T_STOP: f_pos = pos; return MV_OK;
        case T_EXIT:
            if (f_chips == f_full) { f_pos = pos; return MV_WIN; }
            break;
        case T_SWITCH:
            f_sw ^= 1;
            gate = f_sw ? T_GATE_B : T_GATE_A;
            break;
        case T_PORTAL:
            pos = (pos == f_portal0) ? f_portal1 : f_portal0;
            break;
        case T_CHIP: case T_CHIP1: case T_CHIP2:
            f_chips |= chip_bit[t - T_CHIP];
            break;
        case T_ARROW_U: case T_ARROW_R: case T_ARROW_D: case T_ARROW_L:
            delta = dir_dpos[t - T_ARROW_U];
            break;
        default: break;
        }
        if (steps >= SLIDE_MAX) return MV_LOOP;
    }
    if (!steps) return MV_NONE;
    f_pos = pos;
    return MV_OK;
}
#endif

uint8_t solve(const Level *L, const State *from) CORE_BANKED
{
    uint16_t head = 0, tail = 0, layer_end;
    uint16_t id;
    uint8_t depth = 0, d, r, lo;
    State s;

    memset(visited, 0, sizeof visited);

    fast_setup(L);
    id = STATE_ID(from);
    visited[id >> 3] |= bitmask[id & 7];
    queue[tail++] = id;
    solve_visited = 1;

    while (head < tail && depth < SOLVE_MAX_DEPTH) {
        layer_end = tail;
        depth++;
        while (head < layer_end) {
            id = queue[head++];
            lo = (uint8_t)id;
            s.pos = (uint8_t)(lo & 0x7F);
            s.sw = (uint8_t)(lo >> 7);
            s.chips = (uint8_t)(id >> 8);
            for (d = 0; d < 4; d++) {
                f_pos = s.pos; f_chips = s.chips; f_sw = s.sw;
                r = fast_move(d);
                if (r == MV_WIN) return depth;
                if (r != MV_OK) continue;
                id = (uint16_t)((uint8_t)(f_pos | (uint8_t)(f_sw << 7)) | ((uint16_t)f_chips << 8));
                if (visited[id >> 3] & bitmask[(uint8_t)id & 7]) continue;
                visited[id >> 3] |= bitmask[(uint8_t)id & 7];
                solve_visited++;
                if (tail < QUEUE_MAX) queue[tail++] = id;
            }
        }
    }
    return SOLVE_NONE;
}

uint8_t solve_far(const Level *L, const State *from, uint8_t *depth_out) CORE_BANKED
{
    uint16_t head = 0, tail = 0, layer_end;
    uint16_t id;
    uint8_t depth = 0, d, r, lo, i, far = NO_POS, far_depth = 0;
    State s;

    memset(visited, 0, sizeof visited);
    fast_setup(L);
    for (i = 0; i < GN; i++)
        if (f_grid[i] == T_EXIT) f_grid[i] = T_FLOOR;
    id = STATE_ID(from);
    visited[id >> 3] |= bitmask[id & 7];
    queue[tail++] = id;
    solve_visited = 1;

    while (head < tail && depth < SOLVE_MAX_DEPTH) {
        layer_end = tail;
        depth++;
        while (head < layer_end) {
            id = queue[head++];
            lo = (uint8_t)id;
            s.pos = (uint8_t)(lo & 0x7F);
            s.sw = (uint8_t)(lo >> 7);
            s.chips = (uint8_t)(id >> 8);
            for (d = 0; d < 4; d++) {
                f_pos = s.pos; f_chips = s.chips; f_sw = s.sw;
                r = fast_move(d);
                if (r != MV_OK) continue;
                id = (uint16_t)((uint8_t)(f_pos | (uint8_t)(f_sw << 7)) | ((uint16_t)f_chips << 8));
                if (visited[id >> 3] & bitmask[(uint8_t)id & 7]) continue;
                visited[id >> 3] |= bitmask[(uint8_t)id & 7];
                solve_visited++;
                if (tail < QUEUE_MAX) queue[tail++] = id;
                if (f_chips == f_full && f_grid[f_pos] == T_FLOOR && f_pos != from->pos) {
                    far = f_pos;
                    far_depth = depth;
                }
            }
        }
    }
    if (depth_out) *depth_out = far_depth;
    return far;
}

uint8_t solve_hint(const Level *L, const State *from, uint8_t *remaining) CORE_BANKED
{
    uint8_t d, r, best = 0xFF, best_len = 0xFF, len;
    State n;
    for (d = 0; d < 4; d++) {
        n = *from;
        r = sim_move(L, &n, d, 0);
        if (r == MV_WIN) { best = d; best_len = 1; break; }
        if (r != MV_OK) continue;
        len = solve(L, &n);
        if (len != SOLVE_NONE && (uint8_t)(len + 1) < best_len) {
            best_len = (uint8_t)(len + 1);
            best = d;
        }
    }
    if (remaining) *remaining = best_len;
    return best;
}
