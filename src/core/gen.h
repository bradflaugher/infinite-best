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

typedef struct {
    uint8_t par_min;
    uint8_t par_max;
    uint8_t mechs;      /* mechanics to place */
    uint8_t featured;   /* mechanic being introduced (0xFF none) */
} GenParams;

extern const uint8_t mech_unlock[NUM_MECH];

/* unlocked mechanics at a sector */
uint8_t gen_unlocked(uint16_t sector) CORE_BANKED;
/* newly unlocked mechanic exactly at this sector or 0xFF */
uint8_t gen_new_mech(uint16_t sector) CORE_BANKED;
void gen_params(uint16_t sector, GenParams *p) CORE_BANKED;
/* Generate the level for (run_seed, sector). Always produces a solvable level (L->par > 0).
 * progress(attempt) is called between attempts (may be NULL) - used for loading FX. */
void gen_level(Level *L, uint16_t run_seed, uint16_t sector, void (*progress)(uint8_t attempt)) CORE_BANKED;

#endif
