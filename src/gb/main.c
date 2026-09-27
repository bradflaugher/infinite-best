/* INFINITE BEST - an endless sliding-circuit puzzler for Game Boy / Game Boy Color.
 * Bank 0 entry point. Everything big lives in switchable banks (game.c, the
 * generator/solver, art, music); bank 0 keeps interrupts, drawing helpers,
 * sound and the move simulation. */
#include <gb/gb.h>
#include "gfx.h"
#include "../core/rng.h"

void game_main(void) BANKED;

uint8_t gen_row, gen_col;
static const char hexd[] = "0123456789ABCDEF";

/* called by the banked generator between attempts: "decrypting" hex noise */
void gen_progress(uint8_t attempt)
{
    char c[2];
    c[0] = hexd[rng_state & 15];
    c[1] = 0;
    if ((attempt & 1) == 0) {
        txt(1, gen_col, gen_row, c, (uint8_t)(attempt & 2 ? 2 : 0));
        gen_col++;
        if (gen_col > 17) gen_col = 2;
    }
}

void main(void)
{
    game_main();
}
