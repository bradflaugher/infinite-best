#ifdef __SDCC
#pragma bank 255
#endif
#include "gen.h"
#include "rng.h"
#include "solver.h"

const uint8_t mech_unlock[NUM_MECH] = {
    UNLOCK_STOP, UNLOCK_CHIP, UNLOCK_PIT, UNLOCK_ARROW, UNLOCK_GATE, UNLOCK_PORTAL
};

/* Reinforcement sectors ask the newest mechanic to matter for this many attempts,
 * then settle for any in-window board (bounds the extra solves). Teaching sectors
 * always insist. */
#define GEN_FOCUS_ATTEMPTS 12

static Level cand;
static Level cur;      /* the board the hill-climb is standing on */
static Level variant;

uint8_t gen_unlocked(uint16_t sector) CORE_BANKED
{
    uint8_t m, bits = 0;
    for (m = 0; m < NUM_MECH; m++)
        if (sector >= mech_unlock[m]) bits |= MBIT(m);
    return bits;
}

uint8_t gen_new_mech(uint16_t sector) CORE_BANKED
{
    uint8_t m;
    for (m = 0; m < NUM_MECH; m++)
        if (sector == mech_unlock[m]) return m;
    return 0xFF;
}

static uint8_t popcount8(uint8_t v)
{
    uint8_t c = 0;
    while (v) { c += (uint8_t)(v & 1u); v >>= 1; }
    return c;
}

void gen_params(uint16_t sector, GenParams *p) CORE_BANKED
{
    uint8_t unlocked = gen_unlocked(sector);
    uint8_t m, pmin, pmax;
    uint16_t s = sector;

    if (sector <= 35) {
        pmin = (uint8_t)(2 + s / 5);
        pmax = (uint8_t)(pmin + 2 + s / 12);
    } else {
        /* Past the tutorial arc the floor keeps rising, ever more slowly: 9 at s35,
         * 12 at s95, 13 at s245, 14 from s495 on - about the deepest a 10x8 board
         * gets inside the Game Boy's generation budget. Beyond par, depth also comes
         * from more chips and more mechanics per board (see build / below).
         * Inside each 7-sector block the last few push one harder before the breather. */
        if (s > 495) s = 495;
        if (s <= 95) pmin = (uint8_t)(9 + (s - 35) / 20);
        else if (s <= 245) pmin = (uint8_t)(12 + (s - 95) / 150);
        else pmin = (uint8_t)(13 + (s - 245) / 250);
        if (sector % 7 >= 4 && pmin < 13) pmin++;
        pmax = (uint8_t)(pmin + 3);
    }

    p->featured = gen_new_mech(sector);
    p->focus = p->featured;
    p->mechs = 0;

    if (p->featured != 0xFF) {
        /* teaching sector: new mechanic alone, gentler par */
        p->mechs = MBIT(p->featured);
        if (pmin > 3) pmin -= 2;
        if (pmax > pmin + 3) pmax = (uint8_t)(pmin + 3);
    } else if (sector % 7 == 0) {
        /* breather */
        if (pmin > 3) pmin -= (uint8_t)(pmin >= 12 ? 3 : 2);
        if (pmax > pmin + 2) pmax = (uint8_t)(pmin + 2);
        p->mechs = 0;
        for (m = 0; m < NUM_MECH; m++)
            if ((unlocked & MBIT(m)) && rng_range(3) == 0) p->mechs |= MBIT(m);
    } else if (sector < FREEFORM_SECTOR) {
        /* most recent mechanic stays on, older ones come and go */
        for (m = 0; m < NUM_MECH; m++) {
            if (!(unlocked & MBIT(m))) continue;
            if (m + 1 == NUM_MECH || !(unlocked & MBIT(m + 1))) {
                p->mechs |= MBIT(m);
                p->focus = m;   /* the newest idea must keep mattering */
            } else if (rng_range(2)) p->mechs |= MBIT(m);
        }
    } else {
        /* freeform: 2..4 random mechanics, 3..5 once you're deep in */
        uint8_t want = (uint8_t)(2 + rng_range(3) + (sector >= 80));
        uint8_t guard = 0;
        while (popcount8(p->mechs) < want && guard++ < 40)
            p->mechs |= MBIT(rng_range(NUM_MECH));
    }
    p->par_min = pmin;
    p->par_max = pmax;
}

static uint8_t rand_pos(void)
{
    uint8_t x = rng_range(LW);
    return POS(x, rng_range(LH));
}

static uint8_t pick_floor(Level *L)
{
    uint8_t i, c;
    for (i = 0; i < 64; i++) {
        c = rand_pos();
        if (L->cell[c] == T_FLOOR && c != L->start) return c;
    }
    for (c = 0; c < GN; c++)
        if (L->cell[c] == T_FLOOR && c != L->start) return c;
    return NO_POS;
}

static void place(Level *L, uint8_t t, uint8_t count)
{
    uint8_t c;
    while (count--) {
        c = pick_floor(L);
        if (c == NO_POS) return;
        L->cell[c] = t;
    }
}

static void build(Level *L, const GenParams *p, uint16_t sector)
{
    uint8_t n, i, c, d, len, x, y;
    uint8_t mech = p->mechs;

    level_clear(L);
    L->sector = sector;
    L->featured = p->featured;
    L->mechs = mech;

    /* walls: clusters and short segments */
    n = (uint8_t)(5 + rng_range(6));
    for (i = 0; i < n; i++) {
        x = rng_range(LW);
        y = rng_range(LH);
        d = rng_range(4);
        len = (uint8_t)(1 + rng_range(3));
        while (len--) {
            if (x >= LW || y >= LH) break;
            L->cell[POS(x, y)] = T_WALL;
            x = (uint8_t)(x + dir_dx[d]);
            y = (uint8_t)(y + dir_dy[d]);
        }
    }

    L->start = pick_floor(L);
    c = pick_floor(L);
    L->cell[c] = T_EXIT;

    if (mech & MBIT(M_CHIP)) {
        /* 1-2 chips early, 1-3 later, 2-3 deep in (collection order is a puzzle too) */
        n = (uint8_t)(sector < 12 ? 1 + rng_range(2) : sector < 100 ? 1 + rng_range(3) : 2 + rng_range(2));
        for (i = 0; i < n; i++) {
            c = pick_floor(L);
            if (c == NO_POS) break;
            L->cell[c] = (uint8_t)(T_CHIP + L->nchips);
            L->chip_pos[L->nchips++] = c;
        }
    }
    if (mech & MBIT(M_STOP)) place(L, T_STOP, (uint8_t)(1 + rng_range(3)));
    /* pits and routers rarely matter by chance: the focus mechanic gets one more */
    if (mech & MBIT(M_PIT)) place(L, T_PIT, (uint8_t)(2 + (p->focus == M_PIT) + rng_range(4)));
    if (mech & MBIT(M_ARROW)) {
        n = (uint8_t)(1 + (p->focus == M_ARROW) + rng_range(3));
        while (n--) place(L, (uint8_t)(T_ARROW_U + rng_range(4)), 1);
    }
    if (mech & MBIT(M_GATE)) {
        place(L, T_SWITCH, 1);
        if (rng_range(3) == 0) place(L, T_SWITCH, 1);
        place(L, T_GATE_A, (uint8_t)(2 + rng_range(3)));
        place(L, T_GATE_B, (uint8_t)(1 + rng_range(3)));
    }
    if (mech & MBIT(M_PORTAL)) {
        c = pick_floor(L);
        if (c != NO_POS) {
            L->cell[c] = T_PORTAL;
            L->portal[0] = c;
            c = pick_floor(L);
            if (c != NO_POS) { L->cell[c] = T_PORTAL; L->portal[1] = c; }
            else { L->cell[L->portal[0]] = T_FLOOR; L->portal[0] = NO_POS; }
        }
    }
}

/* Remove a mechanic from a copy of the level (turn its tiles into floor). */
static void strip_mech(Level *dst, const Level *src, uint8_t m)
{
    uint8_t i, t, keep;
    *dst = *src;
    for (i = 0; i < GN; i++) {
        t = dst->cell[i];
        keep = 1;
        switch (m) {
        case M_STOP: keep = (uint8_t)(t != T_STOP); break;
        case M_PIT: keep = (uint8_t)(t != T_PIT); break;
        case M_ARROW: keep = (uint8_t)(t < T_ARROW_U || t > T_ARROW_L); break;
        case M_GATE: keep = (uint8_t)(t != T_SWITCH && t != T_GATE_A && t != T_GATE_B); break;
        case M_PORTAL: keep = (uint8_t)(t != T_PORTAL); break;
        default: break;
        }
        if (!keep) dst->cell[i] = T_FLOOR;
    }
    if (m == M_PORTAL) { dst->portal[0] = NO_POS; dst->portal[1] = NO_POS; }
}

/* does the mechanic change the optimal solution? */
static uint8_t mech_matters(const Level *L, uint8_t m)
{
    State s;
    if (m == M_CHIP) return 1; /* chips always gate the exit */
    strip_mech(&variant, L, m);
    state_start(&variant, &s);
    return (uint8_t)(solve(&variant, &s) != L->par);
}

static uint8_t window_dist(uint8_t par, const GenParams *p)
{
    if (par < p->par_min) return (uint8_t)(p->par_min - par);
    if (par > p->par_max) return (uint8_t)(par - p->par_max);
    return 0;
}

uint16_t gen_solves;
uint16_t gen_cost;
#ifndef __SDCC
uint32_t gen_work;
#define COUNT_WORK() (gen_work += solve_visited)
#else
#define COUNT_WORK()
#endif

/* Solve cand and score it: 0 = in the par window (and, when insist is set, the focus
 * mechanic changes the solution), otherwise the distance to the window. 0xFF = unsolvable.
 * Every solve is charged to gen_cost (states visited + a fixed overhead), which is
 * deterministic, so the search stops at the same point on the host and the Game Boy. */
static uint8_t evaluate(const GenParams *p, uint8_t insist)
{
    State s;
    uint8_t par, dist;
    state_start(&cand, &s);
    par = solve(&cand, &s);
    gen_solves++;
    gen_cost += (uint16_t)(solve_visited + GEN_SOLVE_OVERHEAD);
    COUNT_WORK();
    if (par == SOLVE_NONE) return 0xFF;
    cand.par = par;
    dist = window_dist(par, p);
    if (dist == 0 && insist && p->focus != 0xFF) {
        uint8_t m = mech_matters(&cand, p->focus);
        gen_solves++;
        gen_cost += (uint16_t)(solve_visited + GEN_SOLVE_OVERHEAD);
        COUNT_WORK();
        if (!m) dist = 1; /* the new idea must actually change the solution */
    }
    return dist;
}

/* Move cand's exit to the stop furthest from the start (see solve_far). */
static uint8_t exit_far(void)
{
    State s;
    uint8_t i, far, depth;
    state_start(&cand, &s);
    far = solve_far(&cand, &s, &depth);
    gen_solves++;
    gen_cost += (uint16_t)(solve_visited + GEN_SOLVE_OVERHEAD);
    COUNT_WORK();
    if (far == NO_POS) return 0;
    for (i = 0; i < GN; i++)
        if (cand.cell[i] == T_EXIT) cand.cell[i] = T_FLOOR;
    cand.cell[far] = T_EXIT;
    return 1;
}

/* One small random edit of cand: toggle a wall, move the start, move any special
 * tile (exit, chip, pad, pit, router, switch, gate, portal) or turn a router.
 * Returns 0 if it couldn't find an edit to make. */
static uint8_t mutate(void)
{
    uint8_t tries, c, e, t, r, i;
    for (tries = 0; tries < 16; tries++) {
        r = rng_range(10);
        c = rand_pos();
        if (r >= 4 && r < 6) {
            /* wall across a line into the exit: forces a longer approach */
            for (e = 0; e < GN; e++)
                if (cand.cell[e] == T_EXIT) break;
            if (e == GN) continue;
            c = (uint8_t)(r == 4 ? POS(POS_X(c), POS_Y(e)) : POS(POS_X(e), POS_Y(c)));
            if (c == cand.start || cand.cell[c] != T_FLOOR) continue;
            cand.cell[c] = T_WALL;
            return 1;
        }
        if (c == cand.start) continue;
        t = cand.cell[c];
        if (r < 4) {
            if (t == T_FLOOR) { cand.cell[c] = T_WALL; return 1; }
            if (t == T_WALL) { cand.cell[c] = T_FLOOR; return 1; }
            continue;
        }
        if (t != T_FLOOR) {
            if (t >= T_ARROW_U && t <= T_ARROW_L) {
                cand.cell[c] = (uint8_t)(T_ARROW_U + ((t - T_ARROW_U + 1 + rng_range(3)) & 3));
                return 1;
            }
            continue;
        }
        if (r == 6) { cand.start = c; return 1; }
        for (i = 0; i < 16; i++) {
            e = rand_pos();
            t = cand.cell[e];
            if (t != T_FLOOR && t != T_WALL) break;
        }
        if (i == 16) continue;
        cand.cell[c] = t;
        cand.cell[e] = T_FLOOR;
        if (IS_CHIP(t)) cand.chip_pos[t - T_CHIP] = c;
        if (t == T_PORTAL) {
            if (cand.portal[0] == e) cand.portal[0] = c; else cand.portal[1] = c;
        }
        return 1;
    }
    return 0;
}

void gen_level(Level *L, uint16_t run_seed, uint16_t sector, void (*progress)(uint8_t attempt)) CORE_BANKED
{
    GenParams p;
    State s;
    uint8_t attempt, dist, best_dist = 0xFF, cur_dist, insist, limit, stall, fresh, step = 0;

    rng_seed(rng_mix(run_seed, sector));
    gen_params(sector, &p);
    L->par = 0;
    gen_solves = 0;
    gen_cost = 0;

    /* 1) random candidates: early sectors usually land here */
    limit = p.featured != 0xFF ? GEN_TEACH_ATTEMPTS : GEN_RANDOM_ATTEMPTS;
    if (p.par_min >= GEN_DEEP_PAR) limit = GEN_DEEP_RANDOM; /* rolls rarely get this deep: climb sooner */
    /* (keep rolling past the limit until something is solvable to climb from; teaching
     * sectors are small and must land, so they get all their rolls regardless of budget) */
    for (attempt = 0; (attempt < limit || !L->par) && attempt < (limit > GEN_MAX_ATTEMPTS ? limit : GEN_MAX_ATTEMPTS)
         && (p.featured != 0xFF || gen_cost < GEN_BUDGET);
         attempt++) {
        if (progress) progress(step++);
        build(&cand, &p, sector);
        insist = (uint8_t)(p.featured != 0xFF || attempt < GEN_FOCUS_ATTEMPTS);
        dist = evaluate(&p, insist);
        if (dist != 0 && dist != 0xFF && cand.par < p.par_min) {
            /* too shallow: first see how deep this layout can go */
            if (dist < best_dist) { best_dist = dist; *L = cand; }
            if (gen_cost < GEN_BUDGET && exit_far() && gen_cost < GEN_BUDGET) dist = evaluate(&p, insist);
        }
        if (dist < best_dist) {
            best_dist = dist;
            *L = cand;
            if (dist == 0) break;
        }
    }

    /* 2) hill-climb towards the window. An edit is kept when it doesn't move par
     * further away (sideways moves let the climb wander across plateaus); a climb
     * that stalls restarts from a fresh random board. Random boards top out around
     * par 10, the climb is what makes deep sectors deep. */
    if (best_dist != 0 && L->par) {
        cur = *L;
        cur_dist = best_dist;
        stall = 0;
        for (attempt = 0; attempt < GEN_CLIMB_STEPS && gen_cost < GEN_BUDGET; attempt++) {
            if (progress) progress(step++);
            insist = (uint8_t)(p.featured != 0xFF || attempt < GEN_CLIMB_FOCUS_STEPS);
            fresh = (uint8_t)(stall >= GEN_CLIMB_STALL);
            if (fresh) {
                build(&cand, &p, sector);
                stall = 0;
            } else {
                cand = cur;
                if (!mutate()) continue;
                /* now and then re-seat the exit at the far end of the new layout */
                if (cur.par < p.par_min && rng_range(4) == 0) exit_far();
            }
            dist = evaluate(&p, insist);
            if (dist == 0xFF) {
                if (fresh) stall = GEN_CLIMB_STALL; /* unsolvable restart: roll again */
                else stall++;
                continue;
            }
            if (fresh || dist <= cur_dist) {
                if (!fresh && dist == cur_dist) stall++;
                else if (dist < cur_dist) stall = 0;
                cur = cand;
                cur_dist = dist;
            } else stall++;
            if (dist < best_dist) {
                best_dist = dist;
                *L = cand;
                if (dist == 0) break;
            }
        }
    }

    /* 3) nothing solvable at all: simplify hard */
    if (L->par == 0) {
        p.mechs &= MBIT(M_STOP);
        p.featured = 0xFF;
        p.focus = 0xFF;
        for (attempt = 0; attempt < GEN_MAX_ATTEMPTS; attempt++) {
            if (progress) progress(step++);
            build(&cand, &p, sector);
            dist = evaluate(&p, 0);
            if (dist < best_dist) {
                best_dist = dist;
                *L = cand;
                if (dist == 0) break;
            }
        }
    }
    L->attempts = (uint8_t)(gen_solves > 254 ? 254 : gen_solves);

    if (L->par == 0) {
        /* ultimate fallback: a guaranteed two-move room */
        level_clear(L);
        L->sector = sector;
        L->cell[POS(5, 3)] = T_WALL;
        L->start = POS(0, 0);
        L->cell[POS(9, 2)] = T_EXIT;
        L->cell[POS(9, 3)] = T_WALL;
        state_start(L, &s);
        L->par = solve(L, &s);
        L->attempts = 0xFF;
    }
}
