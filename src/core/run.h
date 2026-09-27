#ifndef IB_RUN_H
#define IB_RUN_H
#include <stdint.h>

/* Run-mode economy: every move costs one CYCLE of energy. Rewind is free, but
 * spent energy never comes back ("time only flows forward for your battery").
 * Clearing a sector refunds its par; a BEST (par-perfect) clear pays a bonus that
 * grows with your streak. Hitting zero ends the run. */
#define RUN_START_ENERGY 30
#define RUN_MAX_ENERGY   99
#define RUN_HINT_COST    3
#define RUN_BEST_BONUS   2
#define RUN_STREAK_CAP   4

enum { MODE_RUN = 0, MODE_ZEN };
enum { GRADE_OK = 0, GRADE_GOOD, GRADE_BEST };

typedef struct {
    uint8_t mode;
    uint16_t seed;
    uint16_t sector;     /* current sector, starts at 1 */
    uint8_t energy;
    uint8_t streak;      /* consecutive BEST clears */
    uint8_t best_streak;
    uint16_t bests;      /* total BEST clears */
    uint16_t cleared;
    uint8_t moves;       /* moves made in this sector (including rewound ones) */
    uint8_t over;        /* run ended */
} Run;

void run_start(Run *r, uint8_t mode, uint16_t seed, uint16_t sector);
/* call after every real move; returns 1 if the run just ended (out of energy) */
uint8_t run_on_move(Run *r);
/* grade of a clear using `moves` actually made vs par */
uint8_t run_grade(uint8_t moves, uint8_t par);
/* call on win; returns energy gained (run mode) */
uint8_t run_on_win(Run *r, uint8_t par);
/* returns 1 and charges if a hint is affordable */
uint8_t run_try_hint(Run *r);
/* skip current sector (zen only). returns 1 if allowed */
uint8_t run_skip(Run *r);
/* in run mode: check if a stalled run is over (energy 0 with no win) */
uint8_t run_is_over(const Run *r);

#endif
