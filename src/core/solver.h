#ifndef IB_SOLVER_H
#define IB_SOLVER_H
#include "level.h"

#define SOLVE_MAX_DEPTH 60
#define SOLVE_NONE 0

/* Breadth-first search over (pos, chips, switch).
 * Returns optimal number of moves to win from *from, or SOLVE_NONE (0) if unsolvable.
 * (from == a winning state is impossible: winning ends the level.) */
uint8_t solve(const Level *L, const State *from) CORE_BANKED;
/* Number of distinct states visited by the last solve() (a branching/quality metric). */
extern uint16_t solve_visited;
/* Best next direction from *from (0..3), or 0xFF if unsolvable. *remaining = moves left incl. this one. */
uint8_t solve_hint(const Level *L, const State *from, uint8_t *remaining) CORE_BANKED;

#endif
