/* Host CLI for the puzzle core.
 *   ibgen show <seed> <sector>      ASCII render of a sector
 *   ibgen dump <seed> <sector>      machine-readable: par, start, 80 cell bytes (hex)
 *   ibgen solve <seed> <sector>     optimal move string (U/R/D/L) + par
 *   ibgen stats <seeds> <sectors>   generator statistics
 *   ibgen quality <seeds> <sectors> puzzle-quality metrics per sector band (slow)
 *   ibgen q <seed> <sector>         board + its quality metrics
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/core/level.h"
#include "../src/core/solver.h"
#include "../src/core/gen.h"
#include "../src/core/rng.h"
#include "quality.h"

static const char glyph[NUM_T] = {
    '.', '#', 'E', '*', '*', '*', 'o', '^', '>', 'v', '<', 'S', 'A', 'B', '@', 'X'
};

static void show(const Level *L)
{
    int x, y;
    printf("sector %u  par %u  attempts %u  mechs 0x%02x  featured %d\n",
           L->sector, L->par, L->attempts, L->mechs, L->featured == 0xFF ? -1 : L->featured);
    for (y = 0; y < LH; y++) {
        for (x = 0; x < LW; x++) {
            int c = POS(x, y);
            putchar(c == L->start ? 'P' : glyph[L->cell[c]]);
        }
        putchar('\n');
    }
}

int main(int argc, char **argv)
{
    Level L;
    if (argc < 4) {
        fprintf(stderr, "usage: ibgen show|dump|stats a b\n");
        return 2;
    }
    if (!strcmp(argv[1], "show") || !strcmp(argv[1], "dump")) {
        gen_level(&L, (uint16_t)strtoul(argv[2], 0, 0), (uint16_t)strtoul(argv[3], 0, 0), 0);
        if (argv[1][0] == 's') { show(&L); return 0; }
        printf("%u %u ", L.par, L.start);
        for (int i = 0; i < GN; i++) printf("%02x", L.cell[i]);
        printf("\n");
        return 0;
    }
    if (!strcmp(argv[1], "solve")) {
        State st; uint8_t rem, d, i;
        gen_level(&L, (uint16_t)strtoul(argv[2], 0, 0), (uint16_t)strtoul(argv[3], 0, 0), 0);
        state_start(&L, &st);
        printf("%u ", L.par);
        for (i = 0; i < L.par; i++) {
            d = solve_hint(&L, &st, &rem);
            if (d > 3) return 1;
            putchar("URDL"[d]);
            if (sim_move(&L, &st, d, 0) == MV_WIN) break;
        }
        putchar('\n');
        return 0;
    }
    if (!strcmp(argv[1], "q")) {
        Quality q;
        gen_level(&L, (uint16_t)strtoul(argv[2], 0, 0), (uint16_t)strtoul(argv[3], 0, 0), 0);
        show(&L);
        quality(&L, &q);
        printf("reach %d dead %d first %d/%d sols %.0f stumble %.4f/%.4f greedy %d\n"
               "specials %d idle %d walls %d idle %d touched %d reversal %d revisit %d cost %u\n",
               q.reach, q.dead, q.first_opt, q.first_legal, q.nsol, q.stumble, q.stumble2, q.greedy,
               q.specials, q.idle_special, q.walls, q.idle_wall, q.touched, q.reversal, q.revisit, gen_cost);
        return 0;
    }
    if (!strcmp(argv[1], "quality")) {
        /* bands of sectors; each row averages every seed x sector in the band */
        static const int lo[] = { 1, 3, 11, 22, 36, 101, 0 };
        static const int hi[] = { 2, 10, 21, 35, 100, 300, 0 };
        int seeds = atoi(argv[2]), maxsec = atoi(argv[3]), b;
        printf("band       n   par  reach  dead%%  1opt%%  sols  stumble  stmbl+2  greedyPar%% greedy%%"
               "  idleSp%%  idleW%%  touch/sp  rev%%  revis%%\n");
        for (b = 0; lo[b]; b++) {
            double n = 0, par = 0, reach = 0, dead = 0, one = 0, sols = 0, st = 0, st2 = 0, gp = 0, g = 0;
            double sp = 0, isp = 0, w = 0, iw = 0, tch = 0, rev = 0, rvs = 0;
            int sec, s;
            if (lo[b] > maxsec) break;
            for (sec = lo[b]; sec <= hi[b] && sec <= maxsec; sec++)
                for (s = 1; s <= seeds; s++) {
                    Quality q;
                    gen_level(&L, (uint16_t)(s * 7919), (uint16_t)sec, 0);
                    quality(&L, &q);
                    n++; par += q.par; reach += q.reach; dead += q.reach ? (double)q.dead / q.reach : 0;
                    one += q.first_opt == 1; sols += q.nsol; st += q.stumble; st2 += q.stumble2;
                    gp += q.greedy == 2; g += q.greedy >= 1;
                    sp += q.specials; isp += q.idle_special; w += q.walls; iw += q.idle_wall;
                    tch += q.touched; rev += q.reversal; rvs += q.revisit;
                }
            printf("%3d-%-3d %5.0f %5.2f %6.1f %5.1f %6.1f %5.1f  %7.4f  %7.4f  %8.1f %7.1f  %7.1f %6.1f  %4.2f/%-4.2f %5.1f %6.1f\n",
                   lo[b], hi[b], n, par / n, reach / n, dead * 100 / n, one * 100 / n, sols / n,
                   st / n, st2 / n, gp * 100 / n, g * 100 / n, sp ? isp * 100 / sp : 0, w ? iw * 100 / w : 0,
                   tch / n, sp / n, rev * 100 / n, rvs * 100 / n);
        }
        return 0;
    }
    if (!strcmp(argv[1], "stats")) {
        /* attempts = solver calls; cost = gen_cost (~0.077 frames each on a DMG) */
        int seeds = atoi(argv[2]), sectors = atoi(argv[3]);
        long n = 0, fallback = 0, inwin = 0, total_cost = 0, max_cost = 0, total_solves = 0;
        for (int sec = 1; sec <= sectors; sec++) {
            long spar = 0, scost = 0, smax = 0, swin = 0;
            for (int s = 1; s <= seeds; s++) {
                GenParams p;
                uint16_t seed = (uint16_t)(s * 7919);
                gen_level(&L, seed, (uint16_t)sec, 0);
                rng_seed2(seed, (uint16_t)sec);
                gen_params((uint16_t)sec, &p);
                n++;
                total_solves += gen_solves;
                if (L.attempts == 0xFF) fallback++;
                if (L.par >= p.par_min && L.par <= p.par_max) { inwin++; swin++; }
                spar += L.par;
                scost += gen_cost; total_cost += gen_cost;
                if (gen_cost > smax) smax = gen_cost;
                if (gen_cost > max_cost) max_cost = gen_cost;
            }
            if (sec <= 30 || sec % 25 == 0)
                printf("sector %4d  avg par %5.2f  in window %3ld%%  DMG frames avg %4.0f max %4.0f\n",
                       sec, (double)spar / seeds, swin * 100 / seeds,
                       (double)scost / seeds * 0.077, (double)smax * 0.077);
        }
        printf("levels %ld  in window %ld%%  avg solves %.1f  DMG frames avg %.0f max %.0f  fallback %ld\n",
               n, inwin * 100 / n, (double)total_solves / n, (double)total_cost / n * 0.077,
               (double)max_cost * 0.077, fallback);
        return 0;
    }
    return 2;
}
