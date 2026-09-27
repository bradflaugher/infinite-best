/* Host unit tests for the portable puzzle core.
 * Build: gcc -std=c99 -Wall -Wextra -Isrc/core tests/test_core.c src/core/{level,rng,solver,gen,run}.c -o build/test_core */
#include <stdio.h>
#include <string.h>
#include "level.h"
#include "solver.h"
#include "gen.h"
#include "rng.h"
#include "run.h"

static int passed, failed;
#define CHECK(cond) do { if (cond) passed++; else { failed++; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_EQ(a, b) do { long _a = (long)(a), _b = (long)(b); if (_a == _b) passed++; else { failed++; \
    printf("FAIL %s:%d: %s == %s (%ld vs %ld)\n", __FILE__, __LINE__, #a, #b, _a, _b); } } while (0)

/* Parse a 10x8 ASCII map. P=start . floor # wall E exit * chip o stop ^>v< arrows
 * S switch A gateA B gateB @ portal X pit */
static void parse(Level *L, const char *rows[LH])
{
    int x, y, np = 0;
    level_clear(L);
    for (y = 0; y < LH; y++) for (x = 0; x < LW; x++) {
        uint8_t c = POS(x, y), t = T_FLOOR;
        switch (rows[y][x]) {
        case 'P': L->start = c; break;
        case '#': t = T_WALL; break;
        case 'E': t = T_EXIT; break;
        case '*': t = (uint8_t)(T_CHIP + L->nchips); L->chip_pos[L->nchips++] = c; break;
        case 'o': t = T_STOP; break;
        case '^': t = T_ARROW_U; break;
        case '>': t = T_ARROW_R; break;
        case 'v': t = T_ARROW_D; break;
        case '<': t = T_ARROW_L; break;
        case 'S': t = T_SWITCH; break;
        case 'A': t = T_GATE_A; break;
        case 'B': t = T_GATE_B; break;
        case '@': t = T_PORTAL; L->portal[np++] = c; break;
        case 'X': t = T_PIT; break;
        default: break;
        }
        L->cell[c] = t;
    }
}

static uint8_t P(int x, int y) { return POS(x, y); }

static void test_rng(void)
{
    uint16_t first, i, seen_zero = 0;
    uint32_t period = 0;
    uint16_t hist[4] = {0, 0, 0, 0};
    rng_seed(1);
    first = rng_next();
    rng_seed(1);
    CHECK_EQ(rng_next(), first);          /* deterministic */
    rng_seed(0);
    CHECK(rng_state != 0);               /* zero seed is remapped */
    rng_seed(1);
    do { period++; if (rng_next() == 0) seen_zero = 1; } while (rng_state != 1 && period < 70000);
    CHECK_EQ(period, 65535);             /* full-period xorshift16 */
    CHECK(!seen_zero);
    rng_seed(77);
    for (i = 0; i < 4000; i++) { uint8_t v = rng_range(4); CHECK(v < 4); if (v < 4) hist[v]++; }
    for (i = 0; i < 4; i++) CHECK(hist[i] > 850 && hist[i] < 1150); /* roughly uniform */
    CHECK(rng_mix(1, 2) != rng_mix(2, 1));
    CHECK(rng_mix(5, 6) != 0);
}

static void test_slide_basics(void)
{
    const char *m[LH] = {
        "P....#....",
        "..........",
        "..........",
        "..........",
        "..........",
        "..........",
        "..........",
        ".........E" };
    Level L; State s; Path path;
    parse(&L, m);
    state_start(&L, &s);
    CHECK_EQ(sim_move(&L, &s, DIR_R, &path), MV_OK);
    CHECK_EQ(s.pos, P(4, 0));              /* stops before wall */
    CHECK_EQ(path.len, 4);
    CHECK_EQ(sim_move(&L, &s, DIR_U, 0), MV_NONE); /* edge: no move */
    CHECK_EQ(s.pos, P(4, 0));
    CHECK_EQ(sim_move(&L, &s, DIR_D, 0), MV_OK);
    CHECK_EQ(s.pos, P(4, 7));
    CHECK_EQ(sim_move(&L, &s, DIR_R, &path), MV_WIN); /* open exit catches you */
    CHECK_EQ(s.pos, P(9, 7));
    CHECK(path.step[path.len - 1].ev & EV_EXIT);
    state_start(&L, &s);
    CHECK_EQ(solve(&L, &s), 2);            /* down, right */
}

static void test_chips_lock_exit(void)
{
    const char *m[LH] = {
        "P...E....*",
        "#########.",
        "..........",
        "..........",
        "..........",
        "..........",
        "..........",
        ".........." };
    Level L; State s; Path path;
    parse(&L, m);
    state_start(&L, &s);
    /* sliding right passes over the locked exit, collects chip at the end */
    CHECK_EQ(sim_move(&L, &s, DIR_R, &path), MV_OK);
    CHECK_EQ(s.pos, P(9, 0));
    CHECK_EQ(s.chips, 1);
    CHECK(path.step[path.len - 1].ev & EV_CHIP);
    CHECK(path.step[path.len - 1].ev & EV_UNLOCK);
    /* now the exit is open: sliding back left gets caught */
    CHECK_EQ(sim_move(&L, &s, DIR_L, 0), MV_WIN);
    state_start(&L, &s);
    CHECK_EQ(solve(&L, &s), 2);
}

static void test_stop_and_pit(void)
{
    const char *m[LH] = {
        "P..o..X...",
        "..........",
        "..........",
        "..........",
        "..........",
        "..........",
        "..........",
        "...E......" };
    Level L; State s;
    parse(&L, m);
    state_start(&L, &s);
    CHECK_EQ(sim_move(&L, &s, DIR_R, 0), MV_OK);
    CHECK_EQ(s.pos, P(3, 0));              /* stop pad */
    CHECK_EQ(sim_move(&L, &s, DIR_R, 0), MV_DEAD); /* into the pit */
    state_start(&L, &s);
    CHECK_EQ(sim_move(&L, &s, DIR_R, 0), MV_OK);
    CHECK_EQ(sim_move(&L, &s, DIR_D, 0), MV_WIN);
    state_start(&L, &s);
    CHECK_EQ(solve(&L, &s), 2);
}

static void test_arrows_and_loop(void)
{
    const char *m[LH] = {
        "P...v.....",
        "..........",
        "..........",
        "....>....E",
        "..........",
        "..........",
        ">........<",
        ".........." };
    Level L; State s; Path path;
    parse(&L, m);
    state_start(&L, &s);
    /* right -> hits down arrow at (4,0) -> slides down to (4,3) right arrow -> right into exit */
    CHECK_EQ(sim_move(&L, &s, DIR_R, &path), MV_WIN);
    CHECK_EQ(path.final_dir, DIR_R);
    /* an arrow ping-pong loops forever -> MV_LOOP */
    s.pos = P(0, 5); s.chips = 0; s.sw = 0;
    CHECK_EQ(sim_move(&L, &s, DIR_D, 0), MV_LOOP);
}

static void test_gates(void)
{
    const char *m[LH] = {
        "P.S..A...E",
        "..........",
        "..........",
        "..........",
        "..........",
        "..........",
        "..........",
        "B........." };
    Level L; State s;
    parse(&L, m);
    state_start(&L, &s);
    CHECK(cell_solid(T_GATE_A, 0));
    CHECK(!cell_solid(T_GATE_A, 1));
    CHECK(!cell_solid(T_GATE_B, 0));
    CHECK(cell_solid(T_GATE_B, 1));
    /* passing the switch opens gate A mid-slide */
    CHECK_EQ(sim_move(&L, &s, DIR_R, 0), MV_WIN);
    state_start(&L, &s);
    CHECK_EQ(sim_move(&L, &s, DIR_D, 0), MV_OK);  /* gate B open at sw=0: slides onto it */
    CHECK_EQ(s.pos, P(0, 7));
}

static void test_portal(void)
{
    const char *m[LH] = {
        "P..@......",
        "######.###",
        "..........",
        "..........",
        "..........",
        "..........",
        "......@..E",
        ".........." };
    Level L; State s; Path path;
    parse(&L, m);
    state_start(&L, &s);
    CHECK_EQ(sim_move(&L, &s, DIR_R, &path), MV_WIN);  /* warp to (6,6), keep sliding right */
    {
        int i, tp = 0;
        for (i = 0; i < path.len; i++) if (path.step[i].ev & EV_TELEPORT) tp = 1;
        CHECK(tp);
    }
}

static void test_solver_unsolvable_and_hint(void)
{
    const char *m[LH] = {
        "P.........",
        "..........",
        "..........",
        "..........",
        ".........#",
        "........#E",
        ".........#",
        ".........." };
    const char *m2[LH] = {
        "P....#....",
        "..........",
        "..........",
        "..........",
        "..........",
        "..........",
        "..........",
        ".........E" };
    Level L; State s; uint8_t rem, d;
    parse(&L, m);
    state_start(&L, &s);
    CHECK_EQ(solve(&L, &s), SOLVE_NONE);
    CHECK_EQ(solve_hint(&L, &s, &rem), 0xFF);
    parse(&L, m2);
    state_start(&L, &s);
    d = solve_hint(&L, &s, &rem);
    CHECK_EQ(d, DIR_D);
    CHECK_EQ(rem, 2);
}

/* Replaying hints must reach the exit in exactly `par` moves. */
static void test_generator_levels(void)
{
    Level L; State s;
    uint16_t seed, sector;
    int count = 0;
    for (seed = 1; seed <= 40; seed++) {
        for (sector = 1; sector <= 60; sector += 1 + (sector > 25) * 4) {
            uint8_t i, r = MV_OK, rem, d;
            gen_level(&L, (uint16_t)(seed * 131u), sector, 0);
            CHECK(L.par > 0);
            CHECK(L.cell[L.start] == T_FLOOR);
            CHECK(L.nchips <= MAX_CHIPS);
            /* all chips are marked in the grid */
            for (i = 0; i < L.nchips; i++) CHECK_EQ(L.cell[L.chip_pos[i]], T_CHIP + i);
            /* portals come in pairs */
            CHECK((L.portal[0] == NO_POS) == (L.portal[1] == NO_POS));
            state_start(&L, &s);
            for (i = 0; i < L.par; i++) {
                d = solve_hint(&L, &s, &rem);
                CHECK(d < 4);
                if (d >= 4) break;
                CHECK_EQ(rem, L.par - i);
                r = sim_move(&L, &s, d, 0);
                if (r != MV_OK) break;
            }
            CHECK_EQ(r, MV_WIN);
            CHECK_EQ(i + (r == MV_WIN), L.par);
            count++;
        }
    }
    CHECK(count > 1000);
}

static void test_grid_padding(void)
{
    Level L;
    uint8_t i, walls = 0;
    level_clear(&L);
    for (i = 0; i < GN; i++) walls += (uint8_t)(L.cell[i] == T_WALL);
    CHECK_EQ(walls, GN - LN);          /* border only */
    CHECK_EQ(POS(0, 0), 13);
    CHECK_EQ(POS_X(POS(9, 7)), 9);
    CHECK_EQ(POS_Y(POS(9, 7)), 7);
    CHECK_EQ(NUM_T, 16);
}

static void test_generator_determinism(void)
{
    Level a, b;
    gen_level(&a, 1234, 17, 0);
    gen_level(&b, 1234, 17, 0);
    CHECK(memcmp(&a, &b, sizeof a) == 0);
    gen_level(&b, 1235, 17, 0);
    CHECK(memcmp(a.cell, b.cell, GN) != 0);
    gen_level(&b, 1234, 18, 0);
    CHECK(memcmp(a.cell, b.cell, GN) != 0);
}

static void test_teaching_levels(void)
{
    Level L;
    uint16_t seed;
    uint8_t m;
    for (m = 0; m < NUM_MECH; m++) {
        CHECK_EQ(gen_new_mech(mech_unlock[m]), m);
        for (seed = 1; seed <= 25; seed++) {
            gen_level(&L, seed, mech_unlock[m], 0);
            CHECK_EQ(L.featured, m);
            CHECK(L.mechs & MBIT(m));
        }
    }
    CHECK_EQ(gen_unlocked(1), 0);
    CHECK_EQ(gen_unlocked(1000), (1 << NUM_MECH) - 1);
    CHECK_EQ(gen_new_mech(2), 0xFF);
}

/* par of L with every tile of mechanic m turned into floor */
static uint8_t par_without(const Level *src, uint8_t m)
{
    Level d = *src;
    State s;
    uint8_t i, t, strip;
    for (i = 0; i < GN; i++) {
        t = d.cell[i];
        switch (m) {
        case M_STOP: strip = (uint8_t)(t == T_STOP); break;
        case M_PIT: strip = (uint8_t)(t == T_PIT); break;
        case M_ARROW: strip = (uint8_t)(t >= T_ARROW_U && t <= T_ARROW_L); break;
        case M_GATE: strip = (uint8_t)(t == T_SWITCH || t == T_GATE_A || t == T_GATE_B); break;
        case M_PORTAL: strip = (uint8_t)(t == T_PORTAL); break;
        default: strip = 0; break;
        }
        if (strip) d.cell[i] = T_FLOOR;
    }
    if (m == M_PORTAL) { d.portal[0] = NO_POS; d.portal[1] = NO_POS; }
    state_start(&d, &s);
    return solve(&d, &s);
}

/* The newest mechanic has to change the solution: always on its teaching
 * sector, and usually on the reinforcement sectors that follow it. */
static void test_mechanics_matter(void)
{
    static const uint16_t reinforce[] = { 4, 12, 13, 15, 16, 19, 20 };
    static const uint8_t reinforce_m[] = { M_STOP, M_ARROW, M_ARROW, M_GATE, M_GATE, M_PORTAL, M_PORTAL };
    GenParams p;
    Level L;
    uint16_t seed;
    uint8_t m, i;
    int matter, total = 0;

    for (m = 0; m < NUM_MECH; m++) {
        rng_seed(1); gen_params(mech_unlock[m], &p);
        CHECK_EQ(p.focus, m);
        if (m == M_CHIP) continue;
        for (seed = 1; seed <= 40; seed++) {
            gen_level(&L, (uint16_t)(seed * 977u), mech_unlock[m], 0);
            CHECK(par_without(&L, m) != L.par);
            CHECK(L.par >= 2);
        }
    }
    for (i = 0; i < sizeof reinforce / sizeof reinforce[0]; i++) {
        rng_seed(5); gen_params(reinforce[i], &p);
        CHECK_EQ(p.focus, reinforce_m[i]);
        CHECK(p.mechs & MBIT(reinforce_m[i]));
        matter = 0;
        for (seed = 1; seed <= 40; seed++) {
            gen_level(&L, (uint16_t)(seed * 977u), reinforce[i], 0);
            matter += par_without(&L, reinforce_m[i]) != L.par;
        }
        CHECK(matter >= 22);
        total += matter;
    }
    CHECK(total >= 200);   /* of 280; about 120 before the focus rule */
    rng_seed(1); gen_params(FREEFORM_SECTOR + 1, &p);
    CHECK_EQ(p.focus, 0xFF);
    rng_seed(1); gen_params(1, &p);
    CHECK_EQ(p.focus, 0xFF);
}

static void test_difficulty_curve(void)
{
    GenParams p1, p50;
    long early = 0, late = 0;
    uint16_t seed;
    Level L;
    rng_seed(3); gen_params(1, &p1);
    rng_seed(3); gen_params(50, &p50);
    CHECK(p1.par_min < p50.par_min);
    CHECK(p1.par_max <= p50.par_max);
    for (seed = 1; seed <= 30; seed++) {
        gen_level(&L, seed, 2, 0); early += L.par;
        gen_level(&L, seed, 40, 0); late += L.par;
    }
    CHECK(late > early * 2);
}

/* "Infinite" means no plateau: the par floor keeps rising well past the tutorial
 * arc, deep boards actually reach it, and generation stays inside its budget. */
static void test_endless_curve(void)
{
    static const uint16_t band[] = { 36, 100, 250, 500 };
    long par[4] = { 0, 0, 0, 0 };
    uint16_t seed, sec, prev_min = 0, worst = 0;
    uint8_t b, k, inwin = 0, total = 0;
    GenParams p;
    Level L;
    for (b = 0; b < 4; b++) {
        rng_seed(1); gen_params((uint16_t)(band[b] + 1), &p);
        CHECK(p.par_min > prev_min);
        prev_min = p.par_min;
        for (k = 1; k <= 6; k++) {          /* one 7-sector block, skip the breather */
            sec = (uint16_t)(band[b] + k);
            if (sec % 7 == 0) continue;
            for (seed = 1; seed <= 8; seed++) {
                gen_level(&L, (uint16_t)(seed * 4099u), sec, 0);
                rng_seed(rng_mix((uint16_t)(seed * 4099u), sec)); gen_params(sec, &p);
                par[b] += L.par;
                total++;
                if (L.par >= p.par_min && L.par <= p.par_max) inwin++;
                if (gen_cost > worst) worst = gen_cost;
            }
        }
    }
    CHECK(par[1] > par[0]);
    CHECK(par[2] > par[1]);
    CHECK(par[3] > par[1]);                 /* 250 vs 500 is within noise at 8 seeds */
    CHECK(inwin * 10 >= total * 7);         /* >= 70% land in their window */
    CHECK(worst < GEN_BUDGET + 400);        /* budget is checked between solves */
}

static void test_run_economy(void)
{
    Run r;
    uint8_t i, gain;
    run_start(&r, MODE_RUN, 99, 0);
    CHECK_EQ(r.sector, 1);
    CHECK_EQ(r.energy, RUN_START_ENERGY);
    for (i = 0; i < 3; i++) run_on_move(&r);
    CHECK_EQ(r.energy, RUN_START_ENERGY - 3);
    CHECK_EQ(run_grade(3, 3), GRADE_BEST);
    CHECK_EQ(run_grade(5, 3), GRADE_GOOD);
    CHECK_EQ(run_grade(6, 3), GRADE_OK);
    gain = run_on_win(&r, 3);                 /* par clear: par + bonus + streak(1) */
    CHECK_EQ(gain, 3 + RUN_BEST_BONUS + 1);
    CHECK_EQ(r.streak, 1);
    CHECK_EQ(r.sector, 2);
    CHECK_EQ(r.moves, 0);
    for (i = 0; i < 6; i++) run_on_move(&r);
    gain = run_on_win(&r, 3);                 /* sloppy: only refund par, streak broken */
    CHECK_EQ(gain, 3);
    CHECK_EQ(r.streak, 0);
    CHECK_EQ(r.best_streak, 1);
    CHECK_EQ(r.bests, 1);
    CHECK_EQ(r.cleared, 2);
    /* energy cap */
    r.energy = 98;
    r.moves = 1;
    run_on_win(&r, 9);
    CHECK_EQ(r.energy, RUN_MAX_ENERGY);
    /* hints */
    r.energy = RUN_HINT_COST;
    CHECK(!run_try_hint(&r));
    r.energy = 10;
    CHECK(run_try_hint(&r));
    CHECK_EQ(r.energy, 10 - RUN_HINT_COST);
    /* running dry */
    r.energy = 1;
    run_on_move(&r);
    CHECK(run_is_over(&r));
    CHECK(!run_skip(&r));
    /* zen never runs out, can skip */
    run_start(&r, MODE_ZEN, 1, 7);
    for (i = 0; i < 100; i++) run_on_move(&r);
    CHECK(!run_is_over(&r));
    CHECK(run_skip(&r));
    CHECK_EQ(r.sector, 8);
    CHECK(run_try_hint(&r));
}

int main(void)
{
    test_rng();
    test_slide_basics();
    test_chips_lock_exit();
    test_stop_and_pit();
    test_arrows_and_loop();
    test_gates();
    test_portal();
    test_solver_unsolvable_and_hint();
    test_generator_levels();
    test_grid_padding();
    test_generator_determinism();
    test_teaching_levels();
    test_mechanics_matter();
    test_difficulty_curve();
    test_endless_curve();
    test_run_economy();
    printf("core: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
