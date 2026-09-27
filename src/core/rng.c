#include "rng.h"

uint16_t rng_state = 1;

void rng_seed(uint16_t seed)
{
    rng_state = seed ? seed : 0xACE1u;
}

uint16_t rng_next(void)
{
    uint16_t x = rng_state;
    x ^= (uint16_t)(x << 7);
    x ^= (uint16_t)(x >> 9);
    x ^= (uint16_t)(x << 8);
    rng_state = x;
    return x;
}

uint8_t rng_range(uint8_t n)
{
    /* multiply-high on the top byte: avoids division, bias is negligible for gameplay */
    uint16_t r = (uint16_t)(rng_next() >> 8);
    return (uint8_t)((r * (uint16_t)n) >> 8);
}

uint16_t rng_mix(uint16_t a, uint16_t b)
{
    uint16_t h = (uint16_t)(a ^ 0x9E37u);
    uint8_t i;
    for (i = 0; i < 3; i++) {
        h ^= b;
        h = (uint16_t)(h * 0x6F4Du + 0x3A9Bu);
        h ^= (uint16_t)(h >> 7);
        b = (uint16_t)((b << 5) | (b >> 11));
    }
    return h ? h : 0x1234u;
}
