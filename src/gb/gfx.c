#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include "gfx.h"
#include "assets.h"
#include "sound.h"
#include "../core/rng.h"

BANKREF_EXTERN(assets)

uint8_t is_cgb;
uint8_t mt_pal[NUM_MT];             /* RAM copies of banked lookup tables */
static uint16_t bg_pal[8 * 4];
static uint16_t spr_pal[8 * 4];

#define ASSETS_IN()  uint8_t _bank = CURRENT_BANK; SWITCH_ROM(BANK(assets))
#define ASSETS_OUT() SWITCH_ROM(_bank)
volatile uint8_t frame;
int8_t shake_x, shake_y;

static uint8_t shake_amt;
static uint8_t flash_t;
static volatile uint8_t wave_mode;
static volatile uint8_t wave_phase;
static volatile uint8_t wave_base;
volatile uint8_t wave_top, wave_bottom = HUD_WY;
static uint8_t map_buf[20 * 16];
static uint8_t attr_buf[20 * 16];

static const int8_t wave_tab[64] = {
    0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 0, 0,
    0, 0, 0, -1, -1, -1, -1, -2, -2, -2, -2, -2, -2, -2, -2, -2,
    -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -1, -1, -1, -1, 0, 0
};
static uint8_t glitch_tab[32];

static const uint16_t white_pal[8 * 4] = {
    0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF,
    0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF,
    0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF,
    0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF
};

/* LYC handler: per-band horizontal offset for the Braid-style rewind wave and
 * the death glitch. It chains itself every WAVE_STEP lines inside
 * [wave_top, wave_bottom) and restores the scroll at wave_bottom, so a DMG only
 * pays for ~20-30 interrupts per frame instead of 144 HBlanks. */
#define WAVE_STEP 4
static void lcd_isr(void)
{
    uint8_t ly = LYC_REG, next;
    if (ly >= wave_bottom) {
        SCX_REG = wave_base;
        LYC_REG = wave_top;
        return;
    }
    if (wave_mode == 1)
        SCX_REG = (uint8_t)(wave_base + wave_tab[(uint8_t)(ly + wave_phase) & 63]);
    else
        SCX_REG = (uint8_t)(wave_base + glitch_tab[(uint8_t)((ly >> 2) + wave_phase) & 31]);
    next = (uint8_t)(ly + WAVE_STEP);
    LYC_REG = next > wave_bottom ? wave_bottom : next;
}

static void vbl_isr(void)
{
    frame++;
    wave_base = (uint8_t)shake_x;
    SCX_REG = (uint8_t)shake_x;
    SCY_REG = (uint8_t)shake_y;
    wave_phase++;
    sound_tick();       /* after the timing-sensitive register writes */
}

void gfx_set_palettes(void)
{
    if (is_cgb) {
        set_bkg_palette(0, 8, bg_pal);
        set_sprite_palette(0, 8, spr_pal);
    }
    BGP_REG = 0x1B;
    OBP0_REG = 0x1B;
    OBP1_REG = 0x4B;
}

void gfx_init(void)
{
    DISPLAY_OFF;
    /* _cpu comes from register A at boot; also probe VBK, which reads 0xFF on a DMG */
    VBK_REG = 0;
    is_cgb = (uint8_t)(_cpu == CGB_TYPE && VBK_REG == 0xFE);
    if (is_cgb) cpu_fast();
    {
        ASSETS_IN();
        memcpy(mt_pal, mt_cgb_pal, sizeof mt_pal);
        memcpy(bg_pal, bg_cgb_pal, sizeof bg_pal);
        memcpy(spr_pal, spr_cgb_pal, sizeof spr_pal);
        set_sprite_data(TILE_FONT_BASE, NUM_FONT_TILES, font_tiles);
        set_sprite_data(TILE_UI_BASE, NUM_UI_TILES, ui_tiles);
        set_sprite_data(TILE_SPR_BASE, NUM_SPR_TILES, spr_tiles);
        set_sprite_data(TILE_FADE_BASE, NUM_FADE_TILES, fade_tiles);
        ASSETS_OUT();
    }
    gfx_load_game_tiles();
    gfx_set_palettes();
    clear_bkg();
    clear_win();
    gfx_hide_all_sprites();
    LCDC_REG |= LCDCF_BG8000;
    SPRITES_8x8;
    move_win(7, HUD_WY);
    SHOW_BKG;
    SHOW_WIN;
    SHOW_SPRITES;

    CRITICAL {
        add_VBL(vbl_isr);
        add_LCD(lcd_isr);
        STAT_REG = 0;
    }
    set_interrupts(VBL_IFLAG | LCD_IFLAG);
    DISPLAY_ON;
}

void gfx_load_game_tiles(void)
{
    ASSETS_IN();
    set_sprite_data(TILE_MT_BASE, NUM_MT * 4, mt_tiles);
    ASSETS_OUT();
}

void gfx_load_logo(void)
{
    ASSETS_IN();
    set_sprite_data(TILE_MT_BASE, LOGO_NTILES, logo_tiles);
    ASSETS_OUT();
}

void gfx_draw_logo(uint8_t y)
{
    ASSETS_IN();
    set_bkg_tiles(0, y, LOGO_W, LOGO_H, logo_map);
    if (is_cgb) {
        VBK_REG = VBK_ATTRIBUTES;
        set_bkg_tiles(0, y, LOGO_W, LOGO_H, logo_cgb_attr);
        VBK_REG = VBK_TILES;
    }
    ASSETS_OUT();
}

void gfx_hide_all_sprites(void)
{
    uint8_t i;
    for (i = 0; i < NUM_SPRITES; i++) hide_sprite(i);
}

static void put_tiles(uint8_t win, uint8_t x, uint8_t y, uint8_t w, uint8_t h, const uint8_t *tiles)
{
    if (win) set_win_tiles(x, y, w, h, tiles);
    else set_bkg_tiles(x, y, w, h, tiles);
}

static void put_attr(uint8_t win, uint8_t x, uint8_t y, uint8_t w, uint8_t h, const uint8_t *a)
{
    if (!is_cgb) return;
    VBK_REG = VBK_ATTRIBUTES;
    put_tiles(win, x, y, w, h, a);
    VBK_REG = VBK_TILES;
}

void put_tile(uint8_t win, uint8_t x, uint8_t y, uint8_t tile, uint8_t pal)
{
    put_tiles(win, x, y, 1, 1, &tile);
    put_attr(win, x, y, 1, 1, &pal);
}

void gfill(uint8_t win, uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t tile, uint8_t pal)
{
    uint8_t row[32], prow[32], j;
    for (j = 0; j < w; j++) { row[j] = tile; prow[j] = pal; }
    for (j = 0; j < h; j++) {
        put_tiles(win, x, (uint8_t)(y + j), w, 1, row);
        put_attr(win, x, (uint8_t)(y + j), w, 1, prow);
    }
}

void txt(uint8_t win, uint8_t x, uint8_t y, const char *s, uint8_t pal)
{
    uint8_t buf[20], pbuf[20], n = 0;
    while (*s && n < 20) {
        buf[n] = FONT_TILE(*s);
        pbuf[n] = pal;
        s++; n++;
    }
    if (!n) return;
    put_tiles(win, x, y, n, 1, buf);
    put_attr(win, x, y, n, 1, pbuf);
}

void txt_num(uint8_t win, uint8_t x, uint8_t y, uint16_t v, uint8_t digits, uint8_t pal)
{
    char buf[6];
    uint8_t i;
    if (digits > 5) digits = 5;
    buf[digits] = 0;
    for (i = digits; i > 0; i--) {
        buf[i - 1] = (char)('0' + v % 10);
        v /= 10;
    }
    txt(win, x, y, buf, pal);
}

void txt_hex(uint8_t win, uint8_t x, uint8_t y, uint16_t v, uint8_t pal)
{
    static const char hex[] = "0123456789ABCDEF";
    char buf[5];
    buf[0] = hex[(v >> 12) & 15];
    buf[1] = hex[(v >> 8) & 15];
    buf[2] = hex[(v >> 4) & 15];
    buf[3] = hex[v & 15];
    buf[4] = 0;
    txt(win, x, y, buf, pal);
}

void draw_box(uint8_t win, uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t pal)
{
    gfill(win, x, y, w, h, TILE_BLANK, pal);
    gfill(win, (uint8_t)(x + 1), y, (uint8_t)(w - 2), 1, UI_BOX_T, pal);
    gfill(win, (uint8_t)(x + 1), (uint8_t)(y + h - 1), (uint8_t)(w - 2), 1, UI_BOX_B, pal);
    gfill(win, x, (uint8_t)(y + 1), 1, (uint8_t)(h - 2), UI_BOX_L, pal);
    gfill(win, (uint8_t)(x + w - 1), (uint8_t)(y + 1), 1, (uint8_t)(h - 2), UI_BOX_R, pal);
    put_tile(win, x, y, UI_BOX_TL, pal);
    put_tile(win, (uint8_t)(x + w - 1), y, UI_BOX_TR, pal);
    put_tile(win, x, (uint8_t)(y + h - 1), UI_BOX_BL, pal);
    put_tile(win, (uint8_t)(x + w - 1), (uint8_t)(y + h - 1), UI_BOX_BR, pal);
}

void clear_bkg(void)
{
    gfill(0, 0, 0, 32, 32, TILE_BLANK, PAL_UI);
}

void clear_win(void)
{
    gfill(1, 0, 0, 20, 18, TILE_BLANK, PAL_UI);
}

/* ---------------- level rendering ---------------- */

uint8_t cell_metatile(const Level *L, uint8_t pos, uint8_t chips, uint8_t sw, uint8_t anim)
{
    uint8_t t = L->cell[pos];
    switch (t) {
    case T_WALL:
        return (uint8_t)((((pos * 5u) ^ (pos >> 2)) & 3u) == 0 ? MT_WALL2 : MT_WALL);
    case T_EXIT:
        if (chips != level_all_chips(L)) return MT_EXIT_LOCKED;
        return (uint8_t)(anim & 1 ? MT_EXIT_OPEN2 : MT_EXIT_OPEN);
    case T_CHIP: case T_CHIP1: case T_CHIP2:
        return (uint8_t)((chips & (1u << (t - T_CHIP))) ? MT_FLOOR : MT_CHIP);
    case T_STOP: return MT_STOP;
    case T_ARROW_U: return MT_ARROW_U;
    case T_ARROW_R: return MT_ARROW_R;
    case T_ARROW_D: return MT_ARROW_D;
    case T_ARROW_L: return MT_ARROW_L;
    case T_SWITCH: return (uint8_t)(sw ? MT_SWITCH_1 : MT_SWITCH_0);
    case T_GATE_A: return (uint8_t)(sw ? MT_GATE_A_OPEN : MT_GATE_A_CLOSED);
    case T_GATE_B: return (uint8_t)(sw ? MT_GATE_B_CLOSED : MT_GATE_B_OPEN);
    case T_PORTAL: return MT_PORTAL;
    case T_PIT: return MT_PIT;
    default: return MT_FLOOR;
    }
}

void draw_level(const Level *L, uint8_t chips, uint8_t sw)
{
    uint8_t x, y, m, i, pal;
    uint8_t *mp, *ap;
    for (y = 0; y < LH; y++) {
        mp = &map_buf[(uint16_t)y * 40u];
        ap = &attr_buf[(uint16_t)y * 40u];
        for (x = 0; x < LW; x++) {
            m = cell_metatile(L, POS(x, y), chips, sw, 0);
            i = MT_TILE(m, 0);
            pal = mt_pal[m];
            mp[0] = i; mp[1] = (uint8_t)(i + 1);
            mp[20] = (uint8_t)(i + 2); mp[21] = (uint8_t)(i + 3);
            ap[0] = ap[1] = ap[20] = ap[21] = pal;
            mp += 2; ap += 2;
        }
    }
    set_bkg_tiles(0, 0, 20, 16, map_buf);
    put_attr(0, 0, 0, 20, 16, attr_buf);
}

void draw_cell(const Level *L, uint8_t pos, uint8_t chips, uint8_t sw, uint8_t anim)
{
    uint8_t m = cell_metatile(L, pos, chips, sw, anim);
    uint8_t i = MT_TILE(m, 0), pal = mt_pal[m];
    uint8_t t[4], a[4];
    uint8_t x = (uint8_t)(POS_X(pos) << 1), y = (uint8_t)(POS_Y(pos) << 1);
    t[0] = i; t[1] = (uint8_t)(i + 1); t[2] = (uint8_t)(i + 2); t[3] = (uint8_t)(i + 3);
    a[0] = a[1] = a[2] = a[3] = pal;
    set_bkg_tiles(x, y, 2, 2, t);
    put_attr(0, x, y, 2, 2, a);
}

void draw_dynamic(const Level *L, uint8_t chips, uint8_t sw, uint8_t anim)
{
    uint8_t p, t;
    for (p = POS(0, 0); p <= POS(LW - 1, LH - 1); p++) {
        t = L->cell[p];
        if (t == T_EXIT || t == T_SWITCH || t == T_GATE_A || t == T_GATE_B ||
            t == T_CHIP || t == T_CHIP1 || t == T_CHIP2)
            draw_cell(L, p, chips, sw, anim);
    }
}

/* ---------------- FX ---------------- */

void fx_shake(uint8_t amount)
{
    if (amount > shake_amt) shake_amt = amount;
}

void fx_flash(uint8_t frames)
{
    flash_t = frames;
    if (is_cgb) set_bkg_palette(0, 8, white_pal);
    else BGP_REG = 0x00;
}

void fx_wave(uint8_t mode)
{
    uint8_t i;
    if (mode == 2)
        for (i = 0; i < 32; i++) glitch_tab[i] = (uint8_t)((rng_next() & 7) ? 0 : (rng_next() & 15) - 8);
    wave_mode = mode;
    LYC_REG = wave_top;
    STAT_REG = mode ? STATF_LYC : 0;
    if (!mode) SCX_REG = (uint8_t)shake_x;
}

void fx_update(void)
{
    if (shake_amt) {
        uint8_t r = (uint8_t)rng_next();
        shake_x = (int8_t)((r & 1) ? shake_amt : -(int8_t)shake_amt);
        shake_y = (int8_t)((r & 2) ? (shake_amt >> 1) : -(int8_t)(shake_amt >> 1));
        if ((frame & 1) == 0) shake_amt--;
    } else {
        shake_x = 0;
        shake_y = 0;
    }
    if (flash_t) {
        flash_t--;
        if (!flash_t) {
            if (is_cgb) set_bkg_palette(0, 8, bg_pal);
            else BGP_REG = 0x1B;
        } else if (!is_cgb) {
            BGP_REG = (flash_t & 2) ? 0x00 : 0x6F;
        }
    }
    if (wave_mode == 2 && (frame & 3) == 0) fx_wave(2);
}

/* ---------------- particles ---------------- */

typedef struct {
    int16_t x, y;   /* 1/16 px */
    int8_t vx, vy;
    uint8_t life;
} Part;

static Part parts[NUM_PART];
static uint8_t part_next;

void part_clear(void)
{
    uint8_t i;
    for (i = 0; i < NUM_PART; i++) { parts[i].life = 0; hide_sprite(SP_PART + i); }
}

static const int8_t burst_dx[8] = { 0, 11, 16, 11, 0, -11, -16, -11 };
static const int8_t burst_dy[8] = { -16, -11, 0, 11, 16, 11, 0, -11 };

void part_burst(uint8_t px, uint8_t py, uint8_t n, uint8_t tile, uint8_t pal, uint8_t speed)
{
    uint8_t i, k, j;
    for (i = 0; i < n; i++) {
        k = part_next;
        part_next = (uint8_t)((part_next + 1) & (NUM_PART - 1));
        j = (uint8_t)((i + (rng_next() & 1)) & 7);
        parts[k].x = (int16_t)((uint16_t)px << 4);
        parts[k].y = (int16_t)((uint16_t)py << 4);
        parts[k].vx = (int8_t)((burst_dx[j] * (int8_t)speed) >> 2) + (int8_t)((rng_next() & 7) - 3);
        parts[k].vy = (int8_t)((burst_dy[j] * (int8_t)speed) >> 2) + (int8_t)((rng_next() & 7) - 3);
        parts[k].life = (uint8_t)(14 + (rng_next() & 15));
        set_sprite_tile(SP_PART + k, tile);
        set_sprite_prop(SP_PART + k, pal);
    }
}

void part_update(void)
{
    uint8_t i;
    Part *p = parts;
    for (i = 0; i < NUM_PART; i++, p++) {
        if (!p->life) continue;
        p->life--;
        if (!p->life) { hide_sprite(SP_PART + i); continue; }
        p->x += p->vx;
        p->y += p->vy;
        p->vy += 1;                 /* a little gravity */
        if (p->life == 8) set_sprite_tile(SP_PART + i, SPR_PIXEL);
        move_sprite(SP_PART + i,
                    (uint8_t)((p->x >> 4) + 8 - 4 - shake_x),
                    (uint8_t)((p->y >> 4) + 16 - 4 - shake_y));
    }
}

/* dissolve the playfield with dither tiles, row pairs from the top */
void wipe_out(void)
{
    uint8_t f;
    for (f = 0; f < NUM_FADE_TILES; f += 2) {
        gfill(0, 0, 0, 20, 16, (uint8_t)(TILE_FADE_BASE + f), PAL_UI);
        wait_vbl_done();
    }
}
