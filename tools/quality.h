/* Host-only puzzle quality metrics (used by `ibgen quality` and the tests).
 * Exhaustive and memory-hungry on purpose: this is the yardstick the generator's
 * cheap on-device heuristics are measured against, not something the Game Boy runs. */
#ifndef IB_QUALITY_H
#define IB_QUALITY_H
#include <string.h>
#include "../src/core/level.h"
#include "../src/core/solver.h"

#define Q_NS 2048
#define Q_INF 255

typedef struct {
    int par;
    int reach;          /* reachable states */
    int dead;           /* reachable states from which the exit can never be reached */
    int first_opt;      /* optimal first moves (1 = a single way in) */
    int first_legal;    /* first moves that move at all */
    double nsol;        /* distinct optimal move sequences */
    double stumble;     /* P(random legal moves win within par) = random BEST */
    double stumble2;    /* ... within par + 2 */
    int greedy;         /* 2: greedy "head for the target" play gets par, 1: solves, 0: fails */
    int specials;       /* special tiles (pads, pits, routers, switches, gates, portal pairs) */
    int idle_special;   /* ... whose removal leaves par unchanged */
    int walls;          /* interior walls */
    int idle_wall;      /* ... whose removal leaves par unchanged */
    int reversal;       /* hint solution reverses direction somewhere */
    int revisit;        /* hint solution stops twice on the same cell */
    int touched;        /* special tiles the hint solution passes over */
} Quality;

static int16_t q_succ[Q_NS][4];   /* >=0 next id, -1 no move, -2 crash/loop, -3 win */
static uint8_t q_seen[Q_NS], q_dg[Q_NS];
static uint16_t q_list[Q_NS];

static int16_t q_id(const State *s) { return (int16_t)STATE_ID(s); }
static void q_unid(uint16_t id, State *s)
{
    s->pos = (uint8_t)(id & 0x7F); s->sw = (uint8_t)((id >> 7) & 1); s->chips = (uint8_t)(id >> 8);
}

static int q_par_now(const Level *L)
{
    State s; state_start(L, &s); return solve(L, &s);
}

static int q_special(uint8_t t)
{
    return t == T_STOP || t == T_PIT || (t >= T_ARROW_U && t <= T_ARROW_L) ||
           t == T_SWITCH || t == T_GATE_A || t == T_GATE_B;
}

static int q_abs(int v) { return v < 0 ? -v : v; }
static int popcount_chips(int now, int before)
{
    int c = 0, v = now & ~before;
    while (v) { c += v & 1; v >>= 1; }
    return c;
}

static void quality(const Level *L0, Quality *q)
{
    Level L = *L0;
    State s, n;
    int i, d, nl = 0, changed, k;
    static double ways[Q_NS], P[Q_NS], P2[Q_NS];
    memset(q, 0, sizeof *q);
    q->par = L.par;
    memset(q_seen, 0, sizeof q_seen);
    state_start(&L, &s);
    q_seen[q_id(&s)] = 1;
    q_list[nl++] = (uint16_t)q_id(&s);
    for (i = 0; i < nl; i++) {
        q_unid(q_list[i], &s);
        for (d = 0; d < 4; d++) {
            uint8_t r;
            n = s;
            r = sim_move(&L, &n, (uint8_t)d, 0);
            if (r == MV_NONE) q_succ[q_list[i]][d] = -1;
            else if (r == MV_DEAD || r == MV_LOOP) q_succ[q_list[i]][d] = -2;
            else if (r == MV_WIN) q_succ[q_list[i]][d] = -3;
            else {
                int16_t id = q_id(&n);
                q_succ[q_list[i]][d] = id;
                if (!q_seen[id]) { q_seen[id] = 1; q_list[nl++] = (uint16_t)id; }
            }
        }
    }
    q->reach = nl;
    /* distance to win */
    for (i = 0; i < nl; i++) q_dg[q_list[i]] = Q_INF;
    do {
        changed = 0;
        for (i = 0; i < nl; i++) {
            uint16_t id = q_list[i];
            int best = q_dg[id];
            for (d = 0; d < 4; d++) {
                int16_t t = q_succ[id][d];
                int v = t == -3 ? 1 : t >= 0 && q_dg[t] != Q_INF ? q_dg[t] + 1 : Q_INF;
                if (v < best) best = v;
            }
            if (best != q_dg[id]) { q_dg[id] = (uint8_t)best; changed = 1; }
        }
    } while (changed);
    for (i = 0; i < nl; i++) q->dead += q_dg[q_list[i]] == Q_INF;
    /* optimal sequences and first moves */
    for (k = 1; k <= q->par; k++)
        for (i = 0; i < nl; i++) {
            uint16_t id = q_list[i];
            if (q_dg[id] != k) continue;
            ways[id] = 0;
            for (d = 0; d < 4; d++) {
                int16_t t = q_succ[id][d];
                if (t == -3 && k == 1) ways[id] += 1;
                else if (t >= 0 && q_dg[t] == k - 1) ways[id] += ways[t];
            }
        }
    {
        uint16_t st = q_list[0];
        q->nsol = q_dg[st] == q->par ? ways[st] : 0;
        for (d = 0; d < 4; d++) {
            int16_t t = q_succ[st][d];
            if (t != -1) q->first_legal++;
            if ((t == -3 && q->par == 1) || (t >= 0 && q_dg[t] == q->par - 1)) q->first_opt++;
        }
    }
    /* random play */
    for (i = 0; i < nl; i++) P[q_list[i]] = 0;
    for (k = 1; k <= q->par + 2; k++) {
        for (i = 0; i < nl; i++) {
            uint16_t id = q_list[i];
            double sum = 0; int legal = 0;
            for (d = 0; d < 4; d++) {
                int16_t t = q_succ[id][d];
                if (t == -1) continue;
                legal++;
                sum += t == -3 ? 1 : t == -2 ? P[id] : P[t];
            }
            P2[id] = legal ? sum / legal : 0;
        }
        for (i = 0; i < nl; i++) P[q_list[i]] = P2[q_list[i]];
        if (k == q->par) q->stumble = P[q_list[0]];
    }
    q->stumble2 = P[q_list[0]];
    /* greedy: head for the nearest uncollected chip, else the exit; avoid repeats */
    {
        static uint8_t gv[Q_NS];
        int steps, ex = 0, moves = 0, won = 0;
        for (i = 0; i < GN; i++) if (L.cell[i] == T_EXIT) ex = i;
        memset(gv, 0, sizeof gv);
        state_start(&L, &s);
        gv[q_id(&s)] = 1;
        for (steps = 0; steps < 4 * q->par && !won; steps++) {
            int bd = -1, bscore = 1 << 20, tx, ty, j, bestc = 1 << 20;
            tx = POS_X(ex); ty = POS_Y(ex);
            for (j = 0; j < L.nchips; j++) if (!(s.chips & (1 << j))) {
                int c = L.chip_pos[j];
                int dd = q_abs(POS_X(c) - POS_X(s.pos)) + q_abs(POS_Y(c) - POS_Y(s.pos));
                if (dd < bestc) { bestc = dd; tx = POS_X(c); ty = POS_Y(c); }
            }
            for (d = 0; d < 4; d++) {
                int16_t t = q_succ[q_id(&s)][d];
                int sc;
                if (t == -3) { bd = d; bscore = -1; break; }
                if (t < 0) continue;
                q_unid((uint16_t)t, &n);
                sc = (q_abs(POS_X(n.pos) - tx) + q_abs(POS_Y(n.pos) - ty)) * 4
                     - popcount_chips(n.chips, s.chips) * 100 + (gv[t] ? 1000 : 0);
                if (sc < bscore) { bscore = sc; bd = d; }
            }
            if (bd < 0) break;
            moves++;
            if (q_succ[q_id(&s)][bd] == -3) { won = 1; break; }
            q_unid((uint16_t)q_succ[q_id(&s)][bd], &s);
            gv[q_id(&s)] = 1;
        }
        q->greedy = won ? (moves <= q->par ? 2 : 1) : 0;
    }
    /* idle tiles */
    for (i = 0; i < GN; i++) {
        uint8_t t = L.cell[i];
        if (POS_X(i) == 0xFF || POS_Y(i) == 0xFF) continue;
        if (q_special(t)) {
            q->specials++;
            L.cell[i] = T_FLOOR;
            q->idle_special += q_par_now(&L) == q->par;
            L.cell[i] = t;
        } else if (t == T_WALL) {
            q->walls++;
            L.cell[i] = T_FLOOR;
            q->idle_wall += q_par_now(&L) == q->par;
            L.cell[i] = t;
        }
    }
    if (L.portal[0] != NO_POS) {
        Level V = L;
        q->specials++;
        V.cell[V.portal[0]] = V.cell[V.portal[1]] = T_FLOOR;
        V.portal[0] = V.portal[1] = NO_POS;
        q->idle_special += q_par_now(&V) == q->par;
    }
    /* the hint solution */
    {
        static uint8_t stopped[GN], tch[GN];
        Path path;
        int last = -1, m;
        memset(stopped, 0, sizeof stopped);
        memset(tch, 0, sizeof tch);
        state_start(&L, &s);
        stopped[s.pos] = 1;
        for (m = 0; m < q->par; m++) {
            uint8_t rem, dd = solve_hint(&L, &s, &rem), r;
            if (dd > 3) break;
            if (last >= 0 && ((last + 2) & 3) == dd) q->reversal = 1;
            last = dd;
            r = sim_move(&L, &s, dd, &path);
            for (i = 0; i < path.len; i++) tch[path.step[i].pos] = 1;
            if (r == MV_WIN) break;
            if (stopped[s.pos]) q->revisit = 1;
            stopped[s.pos] = 1;
        }
        for (i = 0; i < GN; i++) q->touched += tch[i] && q_special(L.cell[i]);
        if (L.portal[0] != NO_POS && (tch[L.portal[0]] || tch[L.portal[1]])) q->touched++;
    }
}

#endif
