#ifndef IB_GEN_H
#define IB_GEN_H
#include "level.h"

/* sector where each mechanic is first introduced */
#define UNLOCK_STOP    3
#define UNLOCK_CHIP    5
#define UNLOCK_PIT     8
#define UNLOCK_ARROW  11
#define UNLOCK_GATE   14
#define UNLOCK_PORTAL 18
#define FREEFORM_SECTOR 22

#define GEN_MAX_ATTEMPTS 48
#define GEN_RANDOM_ATTEMPTS 16  /* random candidates before hill-climbing */
#define GEN_CLIMB_STEPS 96      /* mutations of the best candidate */
#define GEN_DEEP_PAR 11         /* from this par floor on... */
#define GEN_DEEP_RANDOM 4       /* ...only this many random rolls before climbing */
#define GEN_CLIMB_FOCUS_STEPS 32
#define GEN_CLIMB_STALL 20      /* fruitless edits before the climb restarts */
#define GEN_SOLVE_OVERHEAD 5    /* per-solve setup, in visited-state units */
#define GEN_BUDGET 2600         /* ~200 frames on a DMG, ~100 on a Color */
#define GEN_TEACH_ATTEMPTS 96   /* teaching sectors roll longer (and ignore GEN_BUDGET) */

typedef struct {
    uint8_t par_min;
    uint8_t par_max;
    uint8_t mechs;      /* mechanics to place */
    uint8_t featured;   /* mechanic being introduced (0xFF none) */
    uint8_t focus;      /* mechanic that must change par (0xFF none) */
} GenParams;

extern const uint8_t mech_unlock[NUM_MECH];

/* unlocked mechanics at a sector */
uint8_t gen_unlocked(uint16_t sector) CORE_BANKED;
/* newly unlocked mechanic exactly at this sector or 0xFF */
uint8_t gen_new_mech(uint16_t sector) CORE_BANKED;
void gen_params(uint16_t sector, GenParams *p) CORE_BANKED;
/* Generate the level for (run_seed, sector). Always produces a solvable level (L->par > 0).
 * progress(attempt) is called between attempts (may be NULL) - used for loading FX. */
/* solver calls used by the last gen_level (stats / timing) */
extern uint16_t gen_solves;
/* generation work (states visited + per-solve overhead) of the last gen_level */
extern uint16_t gen_cost;
void gen_level(Level *L, uint16_t run_seed, uint16_t sector, void (*progress)(uint8_t attempt)) CORE_BANKED;

#endif
