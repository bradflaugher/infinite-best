/* INFINITE BEST - an endless sliding-circuit puzzler for Game Boy / Game Boy Color.
 * Bank 0 entry point. Everything big lives in switchable banks (game.c, the
 * generator/solver, art, music); bank 0 keeps interrupts, drawing helpers,
 * sound and the move simulation. */
#include <gb/gb.h>
#include "gfx.h"

void game_main(void) BANKED;

void main(void)
{
    game_main();
}
