#ifndef IB_SAVE_H
#define IB_SAVE_H
#include <stdint.h>

typedef struct {
    uint8_t magic[2];      /* 'I','B' */
    uint8_t version;
    uint16_t run_best_sector;   /* furthest sector reached in RUN mode */
    uint16_t run_best_bests;
    uint8_t run_best_streak;
    uint16_t zen_sector;
    uint16_t zen_seed;
    uint16_t lifetime_bests;
    uint16_t lifetime_cleared;
    uint8_t checksum;
} SaveData;

extern SaveData save;
void save_load(void);   /* loads or initialises */
void save_write(void);
uint8_t save_checksum(const SaveData *s);

#endif
