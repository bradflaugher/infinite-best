#ifdef __SDCC
#pragma bank 255
#endif
#include "gen.h"
#include "rng.h"
#include "solver.h"

const uint8_t mech_unlock[NUM_MECH] = {
    UNLOCK_STOP, UNLOCK_CHIP, UNLOCK_PIT, UNLOCK_ARROW, UNLOCK_GATE, UNLOCK_PORTAL
};

static Level cand;
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

    if (s > 400) s = 400;
    pmin = (uint8_t)(2 + s / 5);
    if (pmin > 9) pmin = 9;
    pmax = (uint8_t)(pmin + 2 + s / 12);
    if (pmax > pmin + 6) pmax = (uint8_t)(pmin + 6);

    p->featured = gen_new_mech(sector);
    p->mechs = 0;

    if (p->featured != 0xFF) {
        /* teaching sector: new mechanic alone, gentler par */
        p->mechs = MBIT(p->featured);
        if (pmin > 3) pmin -= 2;
        if (pmax > pmin + 3) pmax = (uint8_t)(pmin + 3);
    } else if (sector % 7 == 0) {
        /* breather */
        if (pmin > 3) pmin -= 2;
        if (pmax > pmin + 2) pmax = (uint8_t)(pmin + 2);
        p->mechs = 0;
        for (m = 0; m < NUM_MECH; m++)
            if ((unlocked & MBIT(m)) && rng_range(3) == 0) p->mechs |= MBIT(m);
    } else if (sector < FREEFORM_SECTOR) {
        /* most recent mechanic stays on, older ones come and go */
        for (m = 0; m < NUM_MECH; m++) {
            if (!(unlocked & MBIT(m))) continue;
            if (m + 1 == NUM_MECH || !(unlocked & MBIT(m + 1))) p->mechs |= MBIT(m);
            else if (rng_range(2)) p->mechs |= MBIT(m);
        }
    } else {
        /* freeform: 2..4 random mechanics */
        uint8_t want = (uint8_t)(2 + rng_range(3));
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
        n = (uint8_t)(1 + rng_range((uint8_t)(sector < 12 ? 2 : 3)));
        for (i = 0; i < n; i++) {
            c = pick_floor(L);
            if (c == NO_POS) break;
            L->cell[c] = (uint8_t)(T_CHIP + L->nchips);
            L->chip_pos[L->nchips++] = c;
        }
    }
    if (mech & MBIT(M_STOP)) place(L, T_STOP, (uint8_t)(1 + rng_range(3)));
    if (mech & MBIT(M_PIT)) place(L, T_PIT, (uint8_t)(2 + rng_range(4)));
    if (mech & MBIT(M_ARROW)) {
        n = (uint8_t)(1 + rng_range(3));
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

void gen_level(Level *L, uint16_t run_seed, uint16_t sector, void (*progress)(uint8_t attempt)) CORE_BANKED
{
    GenParams p;
    State s;
    uint8_t attempt, par, dist, best_dist = 0xFF;
    uint8_t phase;

    rng_seed(rng_mix(run_seed, sector));
    gen_params(sector, &p);
    L->par = 0;

    for (phase = 0; phase < 2 && best_dist != 0; phase++) {
        if (phase == 1) {
            if (L->par) break;      /* have a usable fallback already */
            p.mechs &= MBIT(M_STOP); /* simplify hard */
            p.featured = 0xFF;
        }
        for (attempt = 0; attempt < GEN_MAX_ATTEMPTS; attempt++) {
            if (progress) progress(attempt);
            build(&cand, &p, sector);
            state_start(&cand, &s);
            par = solve(&cand, &s);
            if (par == SOLVE_NONE) continue;
            cand.par = par;
            dist = window_dist(par, &p);
            if (dist == 0 && p.featured != 0xFF && !mech_matters(&cand, p.featured))
                dist = 1; /* teaching level must actually use the new idea */
            if (dist < best_dist) {
                best_dist = dist;
                cand.attempts = (uint8_t)(attempt + 1 + phase * GEN_MAX_ATTEMPTS);
                *L = cand;
                if (dist == 0) break;
            }
        }
    }

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
