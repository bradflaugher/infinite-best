#ifndef IB_GFX_H
#define IB_GFX_H
#include <stdint.h>
#include "../core/level.h"

extern uint8_t is_cgb;
extern uint8_t mt_pal[];            /* CGB palette per metatile (RAM copy) */
extern volatile uint8_t frame;      /* incremented in VBL */

/* sprite slots */
#define SP_PLAYER 0     /* 4 */
#define SP_TRAIL 4      /* 3 */
#define SP_HINT 7
#define SP_PART 8       /* 16 particles */
#define NUM_PART 16
#define SP_CURSOR 24
#define SP_MISC 25      /* 8 misc */
#define NUM_SPRITES 40

#define HUD_WY 128

void gfx_init(void);
void gfx_load_game_tiles(void);
void gfx_load_logo(void);
void gfx_draw_logo(uint8_t y);
void gfx_set_palettes(void);
void gfx_hide_all_sprites(void);

/* text on bkg (win=0) or window (win=1); palette used on CGB */
void txt(uint8_t win, uint8_t x, uint8_t y, const char *s, uint8_t pal);
void txt_num(uint8_t win, uint8_t x, uint8_t y, uint16_t v, uint8_t digits, uint8_t pal);
void txt_hex(uint8_t win, uint8_t x, uint8_t y, uint16_t v, uint8_t pal);
void put_tile(uint8_t win, uint8_t x, uint8_t y, uint8_t tile, uint8_t pal);
void gfill(uint8_t win, uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t tile, uint8_t pal);
void draw_box(uint8_t win, uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t pal);
void clear_bkg(void);
void clear_win(void);

/* level rendering; dynamic tiles depend on chips/switch state */
uint8_t cell_metatile(const Level *L, uint8_t pos, uint8_t chips, uint8_t sw, uint8_t anim);
void draw_level(const Level *L, uint8_t chips, uint8_t sw);
void draw_cell(const Level *L, uint8_t pos, uint8_t chips, uint8_t sw, uint8_t anim);
void draw_dynamic(const Level *L, uint8_t chips, uint8_t sw, uint8_t anim);

/* ---- FX ---- */
void fx_shake(uint8_t amount);
void fx_flash(uint8_t frames);
/* VBlank-driven progress bar on window row `row` from column x (16 cells) */
void busy_start(uint8_t x, uint8_t row);
void busy_stop(void);
void fx_wave(uint8_t mode);   /* 0 off, 1 gentle wave, 2 glitch */
void fx_update(void);         /* once per frame from main loop, after wait_vbl_done */
extern int8_t shake_x, shake_y;
extern volatile uint8_t wave_top, wave_bottom;   /* scanline range the wave applies to */

/* particles */
void part_clear(void);
void part_burst(uint8_t px, uint8_t py, uint8_t n, uint8_t tile, uint8_t pal, uint8_t speed);
void part_update(void);

/* wipes */
void wipe_out(void);

#endif
