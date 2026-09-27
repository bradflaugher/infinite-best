/* Host CLI for the puzzle core.
 *   ibgen show <seed> <sector>      ASCII render of a sector
 *   ibgen dump <seed> <sector>      machine-readable: par, start, 80 cell bytes (hex)
 *   ibgen solve <seed> <sector>     optimal move string (U/R/D/L) + par
 *   ibgen stats <seeds> <sectors>   generator statistics
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/core/level.h"
#include "../src/core/solver.h"
#include "../src/core/gen.h"

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
    if (!strcmp(argv[1], "stats")) {
        int seeds = atoi(argv[2]), sectors = atoi(argv[3]);
        long total_att = 0, n = 0, maxatt = 0, fallback = 0, outwin = 0;
        long visits = 0;
        for (int sec = 1; sec <= sectors; sec++) {
            long satt = 0, spar = 0, smax = 0;
            for (int s = 1; s <= seeds; s++) {
                GenParams p;
                gen_level(&L, (uint16_t)(s * 7919), (uint16_t)sec, 0);
                total_att += L.attempts; satt += L.attempts; n++;
                if (L.attempts > maxatt) maxatt = L.attempts;
                if (L.attempts > smax) smax = L.attempts;
                if (L.attempts == 0xFF) fallback++;
                spar += L.par;
                State st; state_start(&L, &st); solve(&L, &st); visits += solve_visited;
                (void)p;
                if (L.attempts > GEN_MAX_ATTEMPTS) outwin++;
            }
            if (sec <= 30 || sec % 25 == 0)
                printf("sector %3d  avg par %5.2f  avg attempts %5.1f  max %ld\n",
                       sec, (double)spar / seeds, (double)satt / seeds, smax);
        }
        printf("levels %ld  avg attempts %.2f  max %ld  phase2 %ld  fallback %ld  avg visited %.1f\n",
               n, (double)total_att / n, maxatt, outwin, fallback, (double)visits / n);
        return 0;
    }
    return 2;
}
