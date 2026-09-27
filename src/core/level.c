#include <string.h>
#include "level.h"

const int8_t dir_dx[4] = { 0, 1, 0, -1 };
const int8_t dir_dy[4] = { -1, 0, 1, 0 };
const int8_t dir_dpos[4] = { -GW, 1, GW, -1 };

#define R(y) y,y,y,y,y,y,y,y,y,y,y,y
const uint8_t pos_y_tab[GN] = { R(0xFF), R(0), R(1), R(2), R(3), R(4), R(5), R(6), R(7), R(0xFF) };
#undef R
#define R 0xFF,0,1,2,3,4,5,6,7,8,9,0xFF
const uint8_t pos_x_tab[GN] = { R, R, R, R, R, R, R, R, R, R };
#undef R

#define W 1
#define R W,0,0,0,0,0,0,0,0,0,0,W
static const uint8_t empty_grid[GN] = {
    W,W,W,W,W,W,W,W,W,W,W,W, R, R, R, R, R, R, R, R, W,W,W,W,W,W,W,W,W,W,W,W };
#undef R
#undef W

/* solidity by cell type for switch state 0 / 1 */
const uint8_t solid_tab0[NUM_T] = { 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0 };
const uint8_t solid_tab1[NUM_T] = { 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0 };

void level_clear(Level *L)
{
    uint8_t i;
    memcpy(L->cell, empty_grid, GN);
    L->start = POS(0, 0);
    L->nchips = 0;
    for (i = 0; i < MAX_CHIPS; i++) L->chip_pos[i] = NO_POS;
    L->portal[0] = NO_POS;
    L->portal[1] = NO_POS;
    L->par = 0;
    L->mechs = 0;
    L->featured = 0xFF;
    L->attempts = 0;
    L->sector = 0;
}

void state_start(const Level *L, State *s)
{
    s->pos = L->start;
    s->chips = 0;
    s->sw = 0;
}

uint8_t level_all_chips(const Level *L)
{
    return (uint8_t)((1u << L->nchips) - 1u);
}

uint8_t cell_solid(uint8_t t, uint8_t sw)
{
    return sw ? solid_tab1[t] : solid_tab0[t];
}

static const uint8_t chip_bit[3] = { 1, 2, 4 };

uint8_t sim_move(const Level *L, State *s, uint8_t dir, Path *path)
{
    const uint8_t *cell = L->cell;
    const uint8_t *solid = s->sw ? solid_tab1 : solid_tab0;
    uint8_t pos = s->pos;
    uint8_t chips = s->chips;
    uint8_t sw = s->sw;
    uint8_t full = level_all_chips(L);
    uint8_t steps = 0;
    uint8_t result = MV_OK;
    int8_t delta = dir_dpos[dir];
    uint8_t np, t, ev;

    if (path) path->len = 0;

    for (;;) {
        np = (uint8_t)(pos + delta);
        t = cell[np];
        if (solid[t]) break;

        pos = np;
        steps++;
        ev = 0;

        if (t != T_FLOOR) {
            switch (t) {
            case T_PIT:
                ev = EV_PIT;
                result = MV_DEAD;
                break;
            case T_CHIP: case T_CHIP1: case T_CHIP2:
                if (!(chips & chip_bit[t - T_CHIP])) {
                    chips |= chip_bit[t - T_CHIP];
                    ev = EV_CHIP;
                    if (chips == full) ev |= EV_UNLOCK;
                }
                break;
            case T_SWITCH:
                sw ^= 1;
                solid = sw ? solid_tab1 : solid_tab0;
                ev = EV_SWITCH;
                break;
            case T_ARROW_U: case T_ARROW_R: case T_ARROW_D: case T_ARROW_L:
                dir = (uint8_t)(t - T_ARROW_U);
                delta = dir_dpos[dir];
                ev = EV_ARROW;
                break;
            case T_STOP:
                ev = EV_STOP;
                result = 0xFE; /* marker: stop after recording */
                break;
            case T_EXIT:
                if (chips == full) { ev = EV_EXIT; result = MV_WIN; }
                break;
            default:
                break;
            }
        }

        if (path && path->len < SLIDE_MAX + 2) {
            path->step[path->len].pos = pos;
            path->step[path->len].ev = ev;
            path->len++;
        }

        if (result != MV_OK) {
            if (result == 0xFE) result = MV_OK;
            break;
        }

        if (t == T_PORTAL) {
            np = (L->portal[0] == pos) ? L->portal[1] : L->portal[0];
            if (np != NO_POS) {
                pos = np;
                if (path && path->len < SLIDE_MAX + 2) {
                    path->step[path->len].pos = pos;
                    path->step[path->len].ev = EV_TELEPORT;
                    path->len++;
                }
            }
        }

        if (steps >= SLIDE_MAX) { result = MV_LOOP; break; }
    }

    if (path) path->final_dir = dir;
    if (steps == 0) return MV_NONE;
    s->pos = pos;
    s->chips = chips;
    s->sw = sw;
    return result;
}
