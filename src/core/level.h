/* INFINITE BEST - portable puzzle core.
 * Compiles with SDCC (GBDK) and host gcc. Keep types explicit: SDCC int is 16 bit. */
#ifndef IB_LEVEL_H
#define IB_LEVEL_H
#include <stdint.h>

/* The generator and solver live in switchable ROM banks on the Game Boy. */
#if defined(__SDCC)
#define CORE_BANKED __banked
#else
#define CORE_BANKED
#endif

/* Play field is 10x8 cells. Internally the grid is padded to 12x10 with a wall
 * border so the slide loop never needs bounds checks or divisions. Positions
 * (State.pos, Level.start, ...) are padded indices; use POS()/POS_X()/POS_Y(). */
#define LW 10
#define LH 8
#define LN 80
#define GW 12
#define GH 10
#define GN 120
#define POS(x, y) ((uint8_t)(((y) + 1) * GW + (x) + 1))
#define POS_X(p) ((uint8_t)(pos_x_tab[(p)]))
#define POS_Y(p) ((uint8_t)(pos_y_tab[(p)]))
#define MAX_CHIPS 3
#define NO_POS 0xFF
#define SLIDE_MAX 48

/* cell types */
enum {
    T_FLOOR = 0, T_WALL, T_EXIT, T_CHIP, T_CHIP1, T_CHIP2, T_STOP,
    T_ARROW_U, T_ARROW_R, T_ARROW_D, T_ARROW_L,
    T_SWITCH, T_GATE_A, T_GATE_B, T_PORTAL, T_PIT,
    NUM_T
};

/* directions */
enum { DIR_U = 0, DIR_R, DIR_D, DIR_L };

/* mechanics (bit ids) in unlock order */
enum { M_STOP = 0, M_CHIP, M_PIT, M_ARROW, M_GATE, M_PORTAL, NUM_MECH };
#define MBIT(m) ((uint8_t)(1u << (m)))

/* move results */
enum { MV_NONE = 0, MV_OK, MV_WIN, MV_DEAD, MV_LOOP };

/* per-step event flags (for animation / sound) */
#define EV_CHIP     0x01
#define EV_SWITCH   0x02
#define EV_ARROW    0x04
#define EV_TELEPORT 0x08  /* this step's pos is the portal exit we warped to */
#define EV_STOP     0x10
#define EV_EXIT     0x20
#define EV_PIT      0x40
#define EV_UNLOCK   0x80  /* last chip collected on this step */

typedef struct {
    uint8_t cell[GN];   /* padded grid; border cells are T_WALL */
    uint8_t start;
    uint8_t nchips;
    uint8_t chip_pos[MAX_CHIPS];
    uint8_t portal[2];
    uint8_t par;        /* optimal number of moves, 0 = unsolved/unsolvable */
    uint8_t mechs;      /* bitmask of MBIT(M_*) present */
    uint8_t featured;   /* mechanic introduced this sector or 0xFF */
    uint8_t attempts;   /* generator attempts used (stats) */
    uint16_t sector;
} Level;

typedef struct {
    uint8_t pos;
    uint8_t chips;      /* collected mask */
    uint8_t sw;         /* switch state 0/1 */
} State;

typedef struct {
    uint8_t pos;
    uint8_t ev;
} Step;

typedef struct {
    uint8_t len;
    uint8_t final_dir;
    Step step[SLIDE_MAX + 2];
} Path;

extern const int8_t dir_dx[4];
extern const int8_t dir_dy[4];

void level_clear(Level *L);
void state_start(const Level *L, State *s);
uint8_t level_all_chips(const Level *L);
/* solid for movement given switch state */
uint8_t cell_solid(uint8_t t, uint8_t sw);
/* Simulate one move. Updates *s. path may be NULL. Returns MV_*. */
uint8_t sim_move(const Level *L, State *s, uint8_t dir, Path *path);
/* compact state id: pos | sw<<7 | chips<<8  (< 2048) */
#define STATE_ID(s) ((uint16_t)(((s)->pos | ((s)->sw << 7)) | ((uint16_t)(s)->chips << 8)))
/* grid helpers */
extern const uint8_t pos_x_tab[GN];
extern const uint8_t pos_y_tab[GN];
extern const int8_t dir_dpos[4];
#define IS_CHIP(t) ((uint8_t)((t) - T_CHIP) < 3)
/* play-field cell (x,y) accessors */
#define CELL_AT(L, x, y) ((L)->cell[POS(x, y)])

#endif
