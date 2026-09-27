#ifndef IB_RNG_H
#define IB_RNG_H
#include <stdint.h>
/* 16-bit xorshift (7,9,8). Deterministic across SDCC and gcc. State must be non-zero. */
extern uint16_t rng_state;
void rng_seed(uint16_t seed);
uint16_t rng_next(void);
/* uniform-ish value in [0, n) for n >= 1 (n <= 255) */
uint8_t rng_range(uint8_t n);
/* mix two 16-bit values into a seed (for per-sector seeds) */
uint16_t rng_mix(uint16_t a, uint16_t b);
#endif
