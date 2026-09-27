/* INFINITE BEST - game front-end: title, generation, play loop, juice.
 * Lives in a switchable ROM bank; the puzzle rules live in src/core. */
#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include "assets.h"
#include "gfx.h"
#include "sound.h"
#include "save.h"
#include "../core/level.h"
#include "../core/solver.h"
#include "../core/gen.h"
#include "../core/rng.h"
#include "../core/run.h"

#include "version.h"   /* generated into build/ by the Makefile: BUILD_DATE */

/* ---- globals (non-static ones are read by the emulator tests via the .sym file) ---- */
enum { GS_BOOT = 0, GS_TITLE, GS_CODEX, GS_GEN, GS_PLAY, GS_OVER };
enum { PS_IDLE = 0, PS_SLIDE, PS_DEAD, PS_WIN, PS_PAUSE, PS_INTRO };

volatile uint8_t game_state;
volatile uint8_t ps;
Level level;
State st;
Run run;
uint16_t dbg_seed;          /* test hook: if non-zero, used as the seed of the next RUN */
uint16_t dbg_gen_frames;    /* frames the last generation took */

static uint8_t keys, prev_keys, pressed;
static State hist[256];          /* ring buffer; uint8_t hist_top wraps for free */
static uint8_t hist_top;
static uint16_t hist_n;
static Path path;
static State pending;
static uint8_t pending_r;
static uint8_t slide_k, slide_speed;
static uint8_t px, py;           /* player pixel position (playfield coords) */
static uint8_t trail_x[3], trail_y[3];
static uint8_t anim_chips, anim_sw;
static uint8_t player_frame, squash_t, blink_t;
static uint8_t hint_dir = 0xFF;
static uint8_t rewind_t;
static uint8_t cur_song = 0xFF;
static uint8_t exit_anim;
static uint16_t seed_acc;
static uint8_t rewinding;        /* B-hold rewind in progress (wave + detune on) */
static uint8_t buf_dir = 0xFF;   /* direction pressed mid-slide, played on landing */
#define BUF_REWIND 4
static uint8_t msg_temp;         /* HUD message is transient: cleared by the next move */
static uint8_t title_sel;        /* title cursor survives trips to codex / seed entry */
static uint16_t record_at_start; /* RUN record before this run, for NEW RECORD! */

static const char *const mech_name[NUM_MECH] = {
    "STOP PAD", "DATA CHIP", "NULL PIT", "ROUTER", "TOGGLE GATES", "PORTAL"
};
static const char *const mech_desc[NUM_MECH] = {   /* <= 16 chars */
    "HALTS YOUR SLIDE",
    "TAKE ALL TO OPEN",
    "TOUCH = SEGFAULT",
    "BENDS YOUR SLIDE",
    "SWITCH FLIPS ALL",
    "WARP, KEEP GOING"
};
static const uint8_t mech_unlock_at[NUM_MECH] = {
    UNLOCK_STOP, UNLOCK_CHIP, UNLOCK_PIT, UNLOCK_ARROW, UNLOCK_GATE, UNLOCK_PORTAL
};
static const uint8_t mech_mt[NUM_MECH] = { MT_STOP, MT_CHIP, MT_PIT, MT_ARROW_R, MT_SWITCH_0, MT_PORTAL };

/* ---------------------------------------------------------------- utils */

static void input(void)
{
    prev_keys = keys;
    keys = joypad();
    pressed = (uint8_t)(keys & ~prev_keys);
}

static void tick(void)
{
    wait_vbl_done();
    fx_update();
    part_update();
    input();
    seed_acc = (uint16_t)(seed_acc * 31u + DIV_REG + keys);
}

static void wait_frames(uint8_t n)
{
    while (n--) tick();
}

static void put_mt(uint8_t win, uint8_t x, uint8_t y, uint8_t m)
{
    uint8_t i = MT_TILE(m, 0), pal = mt_pal[m];
    put_tile(win, x, y, i, pal);
    put_tile(win, (uint8_t)(x + 1), y, (uint8_t)(i + 1), pal);
    put_tile(win, x, (uint8_t)(y + 1), (uint8_t)(i + 2), pal);
    put_tile(win, (uint8_t)(x + 1), (uint8_t)(y + 1), (uint8_t)(i + 3), pal);
}

/* ---------------------------------------------------------------- player sprite */

static void player_show(uint8_t base)
{
    uint8_t x = (uint8_t)(px + 8 - shake_x), y = (uint8_t)(py + 16 - shake_y), i;
    for (i = 0; i < 4; i++) {
        set_sprite_tile(SP_PLAYER + i, (uint8_t)(base + i));
        set_sprite_prop(SP_PLAYER + i, base == SPR_PLAYER_DEAD ? SPAL_DEAD : SPAL_PLAYER);
    }
    move_sprite(SP_PLAYER + 0, x, y);
    move_sprite(SP_PLAYER + 1, (uint8_t)(x + 8), y);
    move_sprite(SP_PLAYER + 2, x, (uint8_t)(y + 8));
    move_sprite(SP_PLAYER + 3, (uint8_t)(x + 8), (uint8_t)(y + 8));
}

static void player_place(uint8_t pos)
{
    px = (uint8_t)(POS_X(pos) << 4);
    py = (uint8_t)(POS_Y(pos) << 4);
}

static void trail_hide(void)
{
    uint8_t i;
    for (i = 0; i < 3; i++) hide_sprite(SP_TRAIL + i);
}

static void hint_hide(void)
{
    hint_dir = 0xFF;
    hide_sprite(SP_HINT);
}

static void hint_draw(void)
{
    uint8_t x = (uint8_t)(px + 8 + 4 - shake_x), y = (uint8_t)(py + 16 + 4 - shake_y);
    if (hint_dir > 3) return;
    if ((frame & 16) == 0) { hide_sprite(SP_HINT); return; }
    x = (uint8_t)(x + dir_dx[hint_dir] * 14);
    y = (uint8_t)(y + dir_dy[hint_dir] * 14);
    if (hint_dir == DIR_U || hint_dir == DIR_D) {
        set_sprite_tile(SP_HINT, SPR_HINT_UP);
        set_sprite_prop(SP_HINT, (uint8_t)(SPAL_HINT | (hint_dir == DIR_D ? S_FLIPY : 0)));
    } else {
        set_sprite_tile(SP_HINT, SPR_HINT_RIGHT);
        set_sprite_prop(SP_HINT, (uint8_t)(SPAL_HINT | (hint_dir == DIR_L ? S_FLIPX : 0)));
    }
    move_sprite(SP_HINT, x, y);
}

/* ---------------------------------------------------------------- HUD (window rows 0-1) */

static void hud_msg(const char *s)
{
    gfill(1, 4, 1, 16, 1, TILE_BLANK, PAL_UI);
    txt(1, 4, 1, s, PAL_UI);
    msg_temp = 1;
}

static void hud_numbers(void)
{
    txt_num(1, 6, 0, level.par, 2, PAL_UI);
    txt_num(1, 10, 0, run.moves > 99 ? 99 : run.moves, 2,
            run.moves > level.par ? PAL_AMBER : PAL_UI);
    if (run.mode == MODE_RUN) {
        txt_num(1, 14, 0, run.energy, 2, run.energy <= 5 ? PAL_PIT : PAL_UI);
    } else {
        put_tile(1, 14, 0, UI_INFINITY_L, PAL_HUD_ACCENT);
        put_tile(1, 15, 0, UI_INFINITY_R, PAL_HUD_ACCENT);
    }
    txt_num(1, 18, 0, run.streak > 99 ? 99 : run.streak, 2, PAL_UI);
}

static void hud_chips(uint8_t chips)
{
    uint8_t i;
    for (i = 0; i < 3; i++) {
        if (i < level.nchips)
            put_tile(1, i, 1, (chips & (1u << i)) ? UI_CHIP : UI_CHIP_EMPTY, PAL_CHIP);
        else
            put_tile(1, i, 1, TILE_BLANK, PAL_UI);
    }
}

static void hud_draw(void)
{
    gfill(1, 0, 0, 20, 2, TILE_BLANK, PAL_UI);
    txt(1, 0, 0, "S", PAL_UI);
    txt_num(1, 1, 0, run.sector > 9999 ? 9999 : run.sector, 4, PAL_UI);
    put_tile(1, 5, 0, UI_FLAG, PAL_HUD_ACCENT);
    put_tile(1, 9, 0, UI_STEP, PAL_HUD_ACCENT);
    put_tile(1, 13, 0, UI_BOLT, PAL_HUD_ACCENT);
    put_tile(1, 17, 0, UI_STAR, PAL_HUD_ACCENT);
    hud_numbers();
    hud_chips(st.chips);
    move_win(7, HUD_WY);
}

/* ---------------------------------------------------------------- history */

static void hist_push(const State *s)
{
    hist[hist_top] = *s;
    hist_top++;
    if (hist_n < 256) hist_n++;
}

static uint8_t hist_pop(State *s)
{
    if (!hist_n) return 0;
    hist_top--;
    hist_n--;
    *s = hist[hist_top];
    return 1;
}

/* ---------------------------------------------------------------- music */

static void music_for_sector(void)
{
    uint8_t want;
    if (run.mode == MODE_ZEN) want = SONG_ZEN;
    else want = (uint8_t)((((run.sector - 1) >> 3) & 1) ? SONG_RUN_B : SONG_RUN_A);
    if (want != cur_song || !music_playing()) {
        cur_song = want;
        music_play(want);
    }
}

/* ---------------------------------------------------------------- generation */

static void generate(uint8_t row)
{
    uint16_t f0 = sys_time;
    gfill(1, 2, row, 16, 1, TILE_BLANK, PAL_UI);   /* sets the CGB attributes too */
    sfx_play(SFX_GEN);
    busy_start(2, row);
    gen_level(&level, run.seed, run.sector, 0);
    busy_stop();
    gfill(1, 2, row, 16, 1, UI_BAR_FULL, PAL_UI);
    dbg_gen_frames = (uint16_t)(sys_time - f0);
}

/* ---------------------------------------------------------------- level start */

static void mech_intro(void)
{
    uint8_t m = level.featured, t = 0;
    for (t = 0; t < 8; t++) hide_sprite(SP_PLAYER + t);
    t = 0;
    move_win(7, 96);
    draw_box(1, 0, 0, 20, 6, PAL_UI);
    txt(1, 2, 1, "NEW PROTOCOL", PAL_AMBER);
    put_mt(1, 2, 2, mech_mt[m]);
    txt(1, 5, 2, mech_name[m], PAL_UI);
    txt(1, 2, 4, mech_desc[m], PAL_UI);
    sfx_play(SFX_NEWMECH);
    ps = PS_INTRO;
    while (t < 200) {
        tick();
        t++;
        if (t > 20 && (pressed & (J_A | J_START | J_B))) break;
        if (keys & (J_UP | J_DOWN | J_LEFT | J_RIGHT)) if (t > 20) break;
    }
    sfx_play(SFX_SELECT);
    hud_draw();
}

static void start_level(void)
{
    gfx_load_game_tiles();
    state_start(&level, &st);
    hist_n = 0;
    hist_top = 0;
    anim_chips = 0;
    anim_sw = 0;
    run.moves = 0;
    hint_hide();
    trail_hide();
    part_clear();
    draw_level(&level, 0, 0);
    player_place(st.pos);
    player_frame = SPR_PLAYER_IDLE;
    player_show(player_frame);
    hud_draw();
    music_for_sector();
    if (run.sector == 1) hud_msg("SLIDE TO EXIT");
    else if (run.sector == 2) hud_msg("B: REWIND");
    else if (run.sector == 3 && level.featured == 0xFF) hud_msg("SELECT: HINT");
    else hud_msg("");
    msg_temp = 0;       /* tutorial lines stay up until something replaces them */
    buf_dir = 0xFF;
    game_state = GS_PLAY;
    if (level.featured != 0xFF) mech_intro();
    ps = PS_IDLE;
}

/* ---------------------------------------------------------------- game over */

static void game_over(void)
{
    uint8_t record = 0, t = 0;
    uint16_t reached = run.sector;

    game_state = GS_OVER;
    ps = PS_IDLE;
    if (run.mode == MODE_RUN) {
        /* win() already raises run_best_sector as you go, so compare against
         * the record as it stood when this run began */
        if (reached > save.run_best_sector) save.run_best_sector = reached;
        record = (uint8_t)(reached > record_at_start);
        if (run.bests > save.run_best_bests) save.run_best_bests = run.bests;
        if (run.best_streak > save.run_best_streak) save.run_best_streak = run.best_streak;
    }
    save_write();

    music_play(SONG_GAMEOVER);
    cur_song = 0xFF;
    fx_wave(2);
    fx_shake(6);
    wait_frames(40);
    fx_wave(0);
    gfx_hide_all_sprites();
    part_clear();
    move_win(7, 144);
    clear_bkg();
    txt(0, 4, 1, "SIGNAL LOST", PAL_PIT);
    txt(0, 3, 5, "SECTOR", PAL_UI);    txt_num(0, 13, 5, reached, 4, PAL_UI);
    txt(0, 3, 7, "CLEARED", PAL_UI);   txt_num(0, 13, 7, run.cleared, 4, PAL_UI);
    put_tile(0, 3, 9, UI_STAR, PAL_HUD_ACCENT);
    txt(0, 4, 9, "BESTS", PAL_UI);     txt_num(0, 13, 9, run.bests, 4, PAL_UI);
    txt(0, 3, 11, "STREAK", PAL_UI);   txt_num(0, 13, 11, run.best_streak, 4, PAL_UI);
    txt(0, 3, 13, "SEED", PAL_UI);     txt_hex(0, 13, 13, run.seed, PAL_CHIP);
    txt(0, 3, 16, "RECORD", PAL_UI);   txt_num(0, 13, 16, save.run_best_sector, 4, PAL_AMBER);
    while (1) {
        tick();
        t++;
        if (record) {
            if (t & 16) txt(0, 4, 3, "NEW RECORD!", PAL_AMBER);
            else gfill(0, 4, 3, 11, 1, TILE_BLANK, PAL_UI);
        }
        if (t > 60 && (pressed & (J_A | J_START))) break;
    }
    sfx_play(SFX_SELECT);
}

/* ---------------------------------------------------------------- moves */

static void land_fx(void)
{
    uint8_t amt, last_ev = path.len ? path.step[path.len - 1].ev : 0;
    squash_t = 8;
    if (last_ev & EV_STOP) {
        sfx_play(SFX_SWITCH);
        part_burst((uint8_t)(px + 8), (uint8_t)(py + 8), 4, SPR_PIXEL, SPAL_SPARK, 1);
        return;
    }
    amt = (uint8_t)(1 + (path.len >> 2));
    if (amt > 4) amt = 4;
    fx_shake(amt);
    sfx_play(SFX_BUMP);
    part_burst((uint8_t)(px + 8 + dir_dx[path.final_dir] * 8), (uint8_t)(py + 8 + dir_dy[path.final_dir] * 8),
               5, SPR_PIXEL, SPAL_SPARK, 2);
}

static void try_move(uint8_t dir, uint8_t buffered)
{
    uint8_t r;
    pending = st;
    r = sim_move(&level, &pending, dir, &path);
    if (r == MV_NONE) {
        if (buffered) return;   /* mashing into the wall mid-slide: no buzz */
        sfx_play(SFX_ERROR);
        fx_shake(1);
        squash_t = 4;
        return;
    }
    hint_hide();
    if (msg_temp) { hud_msg(""); msg_temp = 0; }
    hist_push(&st);
    run_on_move(&run);
    hud_numbers();
    pending_r = r;
    slide_k = 0;
    slide_speed = 2;
    trail_x[0] = trail_x[1] = trail_x[2] = px;
    trail_y[0] = trail_y[1] = trail_y[2] = py;
    sfx_play(SFX_MOVE);
    ps = PS_SLIDE;
}

static void rewind_one(uint8_t first)
{
    if (!hist_pop(&st)) {
        /* complain once per press, not every repeat while B is held */
        if (first) { sfx_play(SFX_ERROR); hud_msg("NO HISTORY"); }
        return;
    }
    anim_chips = st.chips;
    anim_sw = st.sw;
    draw_dynamic(&level, st.chips, st.sw, exit_anim);
    part_burst((uint8_t)(px + 8), (uint8_t)(py + 8), 4, SPR_TRAIL, SPAL_TRAIL, 1);
    player_place(st.pos);
    hud_chips(st.chips);
    hint_hide();
    sfx_play(SFX_REWIND);
    hud_msg("<< REWIND");
}

static void death(void)
{
    uint8_t t;
    ps = PS_DEAD;
    trail_hide();
    player_show(SPR_PLAYER_DEAD);
    sfx_play(SFX_PIT);
    fx_shake(6);
    fx_wave(2);
    hud_msg(pending_r == MV_LOOP ? "STACK OVERFLOW" : "SEGFAULT");
    part_burst((uint8_t)(px + 8), (uint8_t)(py + 8), 10, SPR_PIXEL, SPAL_DEAD, 3);
    for (t = 0; t < 45; t++) {
        tick();
        if ((t & 3) == 0) player_show((t & 4) ? SPR_PLAYER_DEAD : SPR_PLAYER_SQUASH);
    }
    fx_wave(0);
    /* the failed move is undone for free, but the energy is gone */
    hist_pop(&st);
    anim_chips = st.chips;
    anim_sw = st.sw;
    draw_dynamic(&level, st.chips, st.sw, exit_anim);
    player_place(st.pos);
    player_show(SPR_PLAYER_IDLE);
    hud_chips(st.chips);
    sfx_play(SFX_REWIND);
    buf_dir = 0xFF;     /* don't fire a move queued before the crash was visible */
    ps = PS_IDLE;
}

static void win(void)
{
    uint8_t grade, gain, t, wait;
    uint16_t cleared_sector = run.sector;
    uint8_t moves = run.moves;

    ps = PS_WIN;
    trail_hide();
    grade = run_grade(run.moves, level.par);
    gain = run_on_win(&run, level.par);
    sfx_play(grade == GRADE_BEST ? SFX_BEST : SFX_WIN);
    fx_flash(grade == GRADE_BEST ? 10 : 5);
    fx_shake(grade == GRADE_BEST ? 5 : 3);
    part_burst((uint8_t)(px + 8), (uint8_t)(py + 8), 16,
               grade == GRADE_BEST ? SPR_STAR : SPR_SPARK_BIG, SPAL_STAR, 4);

    save.lifetime_cleared++;
    if (grade == GRADE_BEST) save.lifetime_bests++;
    if (run.mode == MODE_ZEN) save.zen_sector = run.sector;
    if (run.mode == MODE_RUN && run.sector > save.run_best_sector) save.run_best_sector = run.sector;
    save_write();

    for (t = 0; t < 36; t++) {
        tick();
        player_show((t & 4) ? SPR_PLAYER_SQUASH : SPR_PLAYER_IDLE);
    }

    /* banner in the window over the bottom half */
    for (t = 0; t < 4; t++) hide_sprite(SP_PLAYER + t);
    draw_box(1, 0, 0, 20, 8, PAL_UI);
    txt(1, 2, 1, "SECTOR", PAL_UI);
    txt_num(1, 9, 1, cleared_sector, 4, PAL_UI);
    txt(1, 14, 1, "CLEAR", PAL_UI);
    if (grade == GRADE_BEST) txt(1, 5, 2, "** BEST! **", PAL_AMBER);
    else if (grade == GRADE_GOOD) txt(1, 7, 2, "GOOD.", PAL_CHIP);
    else txt(1, 6, 2, "SOLVED.", PAL_UI);
    txt(1, 2, 3, "MOVES", PAL_UI);
    txt_num(1, 8, 3, moves > 99 ? 99 : moves, 2, PAL_UI);
    txt(1, 12, 3, "PAR", PAL_UI);
    txt_num(1, 16, 3, level.par, 2, PAL_UI);
    if (run.mode == MODE_RUN) {
        /* new total, then what this clear paid: "*27 +07" */
        put_tile(1, 2, 4, UI_BOLT, PAL_HUD_ACCENT);
        txt_num(1, 3, 4, run.energy, 2, PAL_UI);
        txt(1, 6, 4, "+", PAL_CHIP);
        txt_num(1, 7, 4, gain, 2, PAL_CHIP);
    }
    put_tile(1, 12, 4, UI_STAR, PAL_HUD_ACCENT);
    txt(1, 13, 4, "X", PAL_UI);
    txt_num(1, 14, 4, run.streak > 99 ? 99 : run.streak, 2, PAL_UI);
    txt(1, 2, 5, "COMPILING", PAL_UI);
    move_win(7, 80);
    wait_frames(4);
    game_state = GS_GEN;
    gfill(1, 1, 6, 18, 1, TILE_BLANK, PAL_UI);
    generate(6);
    gfill(1, 1, 5, 18, 2, TILE_BLANK, PAL_UI);
    txt(1, 2, 5, "SECTOR", PAL_UI);
    txt_num(1, 9, 5, run.sector, 4, PAL_AMBER);
    txt(1, 14, 5, "READY", PAL_CHIP);
    /* long enough to read the grade (which was already up while compiling, so a
     * long compile shortens the wait); A skips */
    wait = dbg_gen_frames >= 120 ? 30 : (uint8_t)(150 - dbg_gen_frames);
    for (t = 0; t < wait; t++) {
        tick();
        if (t & 8) txt(1, 6, 6, "A: CONTINUE", PAL_UI);
        else gfill(1, 6, 6, 11, 1, TILE_BLANK, PAL_UI);
        if (t > 8 && (pressed & (J_A | J_START))) break;
    }
    sfx_play(SFX_SELECT);
    gfx_hide_all_sprites();
    part_clear();
    move_win(7, 144);
    wipe_out();
    start_level();
}

static void on_step(uint8_t k)
{
    uint8_t ev = path.step[k].ev, pos = path.step[k].pos, i;
    if (ev & EV_CHIP) {
        for (i = 0; i < level.nchips; i++)
            if (level.chip_pos[i] == pos) anim_chips |= (uint8_t)(1u << i);
        draw_cell(&level, pos, anim_chips, anim_sw, exit_anim);
        part_burst((uint8_t)(px + 8), (uint8_t)(py + 8), 8, SPR_SPARK_SMALL, SPAL_SPARK, 3);
        hud_chips(anim_chips);
        if (ev & EV_UNLOCK) {
            draw_dynamic(&level, anim_chips, anim_sw, exit_anim);
            sfx_play(SFX_UNLOCK);
            fx_flash(3);
            hud_msg("EXIT ONLINE");
        } else {
            sfx_play(SFX_CHIP);
        }
    }
    if (ev & EV_SWITCH) {
        anim_sw ^= 1;
        draw_dynamic(&level, anim_chips, anim_sw, exit_anim);
        sfx_play(SFX_SWITCH);
        fx_shake(1);
    }
    if (ev & EV_ARROW) {
        sfx_play(SFX_ARROW);
        part_burst((uint8_t)(px + 8), (uint8_t)(py + 8), 3, SPR_PIXEL, SPAL_HINT, 1);
    }
}

static void slide_update(void)
{
    uint8_t tx, ty, s;
    Step *step;

    if (slide_k >= path.len || (pending_r == MV_LOOP && slide_k >= 16)) {
        trail_hide();
        if (pending_r == MV_WIN) { st = pending; win(); return; }
        if (pending_r == MV_DEAD || pending_r == MV_LOOP) { death(); goto check_over; }
        st = pending;
        land_fx();
        ps = PS_IDLE;
check_over:
        if (run_is_over(&run)) {
            hud_msg("OUT OF ENERGY");
            wait_frames(50);
            game_over();
            game_state = GS_TITLE;
        }
        return;
    }

    step = &path.step[slide_k];
    tx = (uint8_t)(POS_X(step->pos) << 4);
    ty = (uint8_t)(POS_Y(step->pos) << 4);

    if (step->ev & EV_TELEPORT) {
        part_burst((uint8_t)(px + 8), (uint8_t)(py + 8), 6, SPR_RING_A, SPAL_HINT, 2);
        px = tx; py = ty;
        part_burst((uint8_t)(px + 8), (uint8_t)(py + 8), 6, SPR_RING_B, SPAL_HINT, 2);
        sfx_play(SFX_PORTAL);
        fx_shake(2);
        slide_k++;
        return;
    }

    trail_x[2] = trail_x[1]; trail_y[2] = trail_y[1];
    trail_x[1] = trail_x[0]; trail_y[1] = trail_y[0];
    trail_x[0] = px; trail_y[0] = py;

    s = slide_speed;
    if (slide_speed < 8) slide_speed++;
    /* step toward the target without unsigned under/overflow */
    if (px < tx) px = (uint8_t)(tx - px <= s ? tx : px + s);
    else if (px > tx) px = (uint8_t)(px - tx <= s ? tx : px - s);
    if (py < ty) py = (uint8_t)(ty - py <= s ? ty : py + s);
    else if (py > ty) py = (uint8_t)(py - ty <= s ? ty : py - s);

    {
        uint8_t i;
        for (i = 0; i < 3; i++) {
            set_sprite_tile(SP_TRAIL + i, SPR_TRAIL);
            set_sprite_prop(SP_TRAIL + i, SPAL_TRAIL);
            move_sprite(SP_TRAIL + i, (uint8_t)(trail_x[i] + 12 - shake_x), (uint8_t)(trail_y[i] + 20 - shake_y));
        }
    }

    if (px == tx && py == ty) {
        on_step(slide_k);
        slide_k++;
    }
}

/* ---------------------------------------------------------------- pause */

static uint8_t pause_menu(void)
{
    static const char *const items_run[] = { "RESUME", "RESTART", "HINT  -3", "ABANDON" };
    static const char *const items_zen[] = { "RESUME", "RESTART", "SKIP", "TITLE" };
    const char *const *items = run.mode == MODE_RUN ? items_run : items_zen;
    uint8_t sel = 0, i;

    ps = PS_PAUSE;
    for (i = 0; i < 8; i++) hide_sprite(SP_PLAYER + i);   /* player, trail, hint */
    part_clear();
    music_set_muffle(1);
    sfx_play(SFX_MENU);
    draw_box(1, 0, 0, 20, 8, PAL_UI);
    txt(1, 2, 0, " PAUSED ", PAL_AMBER);
    for (i = 0; i < 4; i++) txt(1, 4, (uint8_t)(1 + i), items[i], PAL_UI);
    txt(1, 1, 6, "SEED", PAL_UI);          /* 18 chars fill the box interior */
    txt_hex(1, 6, 6, run.seed, PAL_CHIP);
    txt(1, 11, 6, BUILD_DATE + 2, PAL_WALL);
    move_win(7, 80);
    for (;;) {
        tick();
        set_sprite_tile(SP_CURSOR, SPR_CURSOR);
        set_sprite_prop(SP_CURSOR, SPAL_CURSOR);
        move_sprite(SP_CURSOR, (uint8_t)(8 + 16 + ((frame >> 3) & 1)), (uint8_t)(16 + 80 + 8 + sel * 8));
        if (pressed & J_UP) { sel = (uint8_t)((sel + 3) & 3); sfx_play(SFX_MENU); }
        if (pressed & J_DOWN) { sel = (uint8_t)((sel + 1) & 3); sfx_play(SFX_MENU); }
        if (pressed & (J_B | J_START)) { sel = 0; break; }
        if (pressed & J_A) break;
    }
    hide_sprite(SP_CURSOR);
    music_set_muffle(0);
    sfx_play(SFX_SELECT);
    hud_draw();
    ps = PS_IDLE;
    return sel;
}

static void do_hint(void)
{
    uint8_t rem;
    if (!run_try_hint(&run)) {
        sfx_play(SFX_ERROR);
        hud_msg("LOW ENERGY");
        return;
    }
    hud_msg("TRACING...");
    hint_dir = solve_hint(&level, &st, &rem);
    hud_numbers();
    if (hint_dir > 3) {
        hud_msg("DEAD END: REWIND");
        sfx_play(SFX_ERROR);
        return;
    }
    sfx_play(SFX_HINT);
    hud_msg("MOVES LEFT:");
    txt_num(1, 16, 1, rem, 2, PAL_CHIP);
}

/* ---------------------------------------------------------------- play loop */

static void play(void)
{
    uint8_t sel;
    record_at_start = save.run_best_sector;
    start_level();
    while (game_state == GS_PLAY) {
        tick();
        if ((frame & 15) == 0 && st.chips == level_all_chips(&level)) {
            uint8_t p;
            exit_anim ^= 1;
            for (p = POS(0, 0); p <= POS(LW - 1, LH - 1); p++)
                if (level.cell[p] == T_EXIT) draw_cell(&level, p, anim_chips, anim_sw, exit_anim);
        }
        if (run.mode == MODE_RUN && run.energy <= 5 && (frame & 15) == 0)
            put_tile(1, 13, 0, (frame & 16) ? UI_BOLT : TILE_BLANK, PAL_PIT);

        /* a rewind starts only on a fresh B press, so a B held over from a
         * banner or a slide doesn't silently rewind without the wave/detune */
        if (rewinding && !(keys & J_B)) {
            rewinding = 0;
            fx_wave(0);
            music_set_rewind(0);
        }
        switch (ps) {
        case PS_IDLE:
            if (buf_dir == BUF_REWIND) {         /* B went down mid-slide */
                buf_dir = 0xFF;
                if (keys & J_B) pressed |= J_B;
            }
            if (buf_dir <= 3) {
                /* a direction tapped mid-slide plays as soon as we land */
                sel = buf_dir;
                buf_dir = 0xFF;
                if (!(keys & J_B)) try_move(sel, 1);
            } else if (pressed & J_B) {
                buf_dir = 0xFF;
                rewinding = 1; rewind_t = 0;
                fx_wave(1); music_set_rewind(1);
                rewind_one(1);
            } else if (rewinding) {
                if (++rewind_t >= 10) { rewind_t = 0; rewind_one(0); }
            } else if (pressed & J_UP) try_move(DIR_U, 0);
            else if (pressed & J_RIGHT) try_move(DIR_R, 0);
            else if (pressed & J_DOWN) try_move(DIR_D, 0);
            else if (pressed & J_LEFT) try_move(DIR_L, 0);
            else if (pressed & J_SELECT) do_hint();
            else if (pressed & J_START) {
                sel = pause_menu();
                if (sel == 1) {
                    hist_push(&st);
                    hint_hide();
                    state_start(&level, &st);
                    anim_chips = 0; anim_sw = 0;
                    draw_dynamic(&level, 0, 0, exit_anim);
                    player_place(st.pos);
                    hud_chips(0);
                    sfx_play(SFX_REWIND);
                    hud_msg("REBOOTED");
                } else if (sel == 2) {
                    if (run.mode == MODE_RUN) do_hint();
                    else {
                        run_skip(&run);
                        save.zen_sector = run.sector;
                        save_write();
                        game_state = GS_GEN;
                        move_win(7, 80);
                        draw_box(1, 0, 0, 20, 8, PAL_UI);
                        txt(1, 2, 1, "SKIPPING SECTOR", PAL_UI);
                        generate(3);
                        gfx_hide_all_sprites();
                        move_win(7, 144);
                        wipe_out();
                        start_level();
                    }
                } else if (sel == 3) {
                    if (run.mode == MODE_RUN) game_over();
                    game_state = GS_TITLE;
                }
            }
            if (ps == PS_IDLE) {
                if (squash_t) { squash_t--; player_frame = SPR_PLAYER_SQUASH; }
                else if (blink_t) { blink_t--; player_frame = SPR_PLAYER_BLINK; }
                else {
                    player_frame = SPR_PLAYER_IDLE;
                    if ((uint8_t)(frame + seed_acc) == 0) blink_t = 6;
                }
            }
            break;
        case PS_SLIDE:
            if (pressed & J_B) buf_dir = BUF_REWIND;
            else if (pressed & J_UP) buf_dir = DIR_U;
            else if (pressed & J_RIGHT) buf_dir = DIR_R;
            else if (pressed & J_DOWN) buf_dir = DIR_D;
            else if (pressed & J_LEFT) buf_dir = DIR_L;
            slide_update();
            player_frame = SPR_PLAYER_IDLE;
            break;
        default:
            break;
        }
        if (game_state == GS_PLAY) {
            player_show(player_frame);
            hint_draw();
        }
    }
    fx_wave(0);
    music_set_rewind(0);
    rewinding = 0;
}

/* ---------------------------------------------------------------- title & codex */

static void codex(void)
{
    uint8_t page = 0, m, y, known;
    uint16_t seen = save.run_best_sector > save.zen_sector ? save.run_best_sector : save.zen_sector;
    game_state = GS_CODEX;
    gfx_load_game_tiles();
    gfx_hide_all_sprites();
    part_clear();       /* title sparkles would otherwise keep flying over the text */
    move_win(7, 144);
    for (;;) {
        clear_bkg();
        if (page == 0) {
            txt(0, 1, 0, "HOW IT WORKS", PAL_AMBER);
            txt(0, 1, 2, "YOU ARE A PACKET.", PAL_UI);
            txt(0, 1, 3, "YOU SLIDE UNTIL", PAL_UI);
            txt(0, 1, 4, "SOMETHING STOPS YOU", PAL_UI);
            txt(0, 1, 5, "REACH THE EXIT.", PAL_UI);
            txt(0, 1, 7, "PAR = FEWEST MOVES", PAL_UI);
            txt(0, 1, 8, "MATCH IT FOR BEST", PAL_AMBER);
            txt(0, 1, 10, "RUN: A MOVE COSTS", PAL_UI);
            txt(0, 1, 11, "1 ENERGY. CLEARS", PAL_UI);
            txt(0, 1, 12, "RECHARGE. REWIND", PAL_UI);
            txt(0, 1, 13, "IS FREE. TIME ISN'T", PAL_UI);
            txt(0, 1, 15, "B REWIND  SEL HINT", PAL_CHIP);
            txt(0, 1, 17, "RIGHT: TILES", PAL_UI);
        } else {
            txt(0, 1, 0, "CODEX", PAL_AMBER);
            txt(0, 13, 0, "< RULES", PAL_WALL);
            put_mt(0, 0, 2, MT_WALL);   txt(0, 3, 2, "WALL", PAL_UI);   txt(0, 3, 3, "STOPS YOU", PAL_UI);
            put_mt(0, 0, 4, MT_EXIT_OPEN); txt(0, 3, 4, "EXIT", PAL_UI); txt(0, 3, 5, "CATCHES YOU", PAL_UI);
            for (m = 0; m < NUM_MECH; m++) {
                y = (uint8_t)(6 + m * 2);
                known = (uint8_t)(seen >= mech_unlock_at[m]);
                if (known) {
                    put_mt(0, 0, y, mech_mt[m]);
                    txt(0, 3, y, mech_name[m], PAL_AMBER);
                    txt(0, 3, (uint8_t)(y + 1), mech_desc[m], PAL_UI);
                } else {
                    txt(0, 0, y, "??", PAL_WALL);
                    txt(0, 3, y, "UNDISCOVERED", PAL_UI);
                    txt(0, 3, (uint8_t)(y + 1), "SECTOR", PAL_WALL);
                    txt_num(0, 10, (uint8_t)(y + 1), mech_unlock_at[m], 2, PAL_WALL);
                }
            }
        }
        for (;;) {
            tick();
            if (pressed & (J_LEFT | J_RIGHT)) { page ^= 1; sfx_play(SFX_MENU); break; }
            if (pressed & (J_B | J_START | J_A)) { sfx_play(SFX_SELECT); return; }
        }
    }
}

static uint8_t title(void)
{
    static const char *const items[4] = { "RUN", "ZEN", "SEED", "CODEX" };
    uint8_t sel = title_sel, t = 0, i;

    game_state = GS_TITLE;
    ps = PS_IDLE;
    gfx_hide_all_sprites();
    part_clear();
    move_win(7, 144);
    clear_bkg();
    gfx_load_logo();
    gfx_draw_logo(1);
    for (i = 0; i < 4; i++) txt(0, 8, (uint8_t)(9 + i * 2), items[i], PAL_UI);
    txt(0, 13, 11, "S", PAL_UI);
    txt_num(0, 14, 11, save.zen_sector, 4, PAL_UI);
    txt(0, 1, 17, "RECORD", PAL_UI);
    txt_num(0, 8, 17, save.run_best_sector, 4, PAL_AMBER);
    put_tile(0, 13, 17, UI_STAR, PAL_HUD_ACCENT);
    txt_num(0, 14, 17, save.lifetime_bests, 5, PAL_UI);
    wave_top = 8;
    wave_bottom = (uint8_t)(8 + LOGO_H * 8);
    fx_wave(1);
    if (cur_song != SONG_TITLE || !music_playing()) { music_play(SONG_TITLE); cur_song = SONG_TITLE; }

    for (;;) {
        tick();
        t++;
        set_sprite_tile(SP_CURSOR, SPR_CURSOR);
        set_sprite_prop(SP_CURSOR, SPAL_CURSOR);
        move_sprite(SP_CURSOR, (uint8_t)(8 + 48 + ((t >> 3) & 1)), (uint8_t)(16 + 72 + sel * 16));
        if ((t & 15) == 0)
            part_burst((uint8_t)(16 + (rng_next() & 127)), (uint8_t)(8 + (rng_next() & 31)), 2,
                       SPR_SPARK_SMALL, (uint8_t)(SPAL_SPARK + (t & 16 ? 2 : 0)), 1);
        if (pressed & J_UP) { sel = (uint8_t)(sel ? sel - 1 : 3); sfx_play(SFX_MENU); }
        if (pressed & (J_DOWN | J_SELECT)) { sel = (uint8_t)(sel == 3 ? 0 : sel + 1); sfx_play(SFX_MENU); }
        if (pressed & (J_A | J_START)) break;
    }
    sfx_play(SFX_SELECT);
    fx_wave(0);
    wave_top = 0;
    wave_bottom = HUD_WY;
    hide_sprite(SP_CURSOR);
    title_sel = sel;
    return sel;
}

/* Type a 4-hex-digit seed (like punching keys on a 12C): share a run with
 * friends and family and compare BEST counts. Returns 0 on cancel. */
static uint16_t seed_entry(void)
{
    static const char hex[] = "0123456789ABCDEF";
    uint8_t dig[4], cur = 0, i, bob, redraw = 1;
    uint16_t v = run.seed ? run.seed : 0x1D0B;
    char c[2];
    game_state = GS_CODEX;
    for (i = 0; i < 4; i++) dig[i] = (uint8_t)((v >> (12 - i * 4)) & 15);
    gfx_hide_all_sprites();
    part_clear();
    clear_bkg();
    txt(0, 5, 2, "ENTER SEED", PAL_AMBER);
    txt(0, 2, 12, "SAME SEED, SAME", PAL_UI);
    txt(0, 2, 13, "SECTORS. RACE YOUR", PAL_UI);
    txt(0, 2, 14, "FAMILY FOR BESTS.", PAL_UI);
    txt(0, 2, 16, "A: RUN   B: BACK", PAL_CHIP);
    c[1] = 0;
    for (;;) {
        if (redraw) {
            for (i = 0; i < 4; i++) {
                c[0] = hex[dig[i]];
                txt(0, (uint8_t)(6 + i * 2), 7, c, i == cur ? PAL_AMBER : PAL_UI);
            }
            redraw = 0;
        }
        tick();
        /* up / down arrows (sprites, so the lower one can flip) on the digit */
        i = (uint8_t)(8 + 48 + cur * 16);
        bob = (uint8_t)((frame >> 4) & 1);
        set_sprite_tile(SP_MISC, SPR_HINT_UP);
        set_sprite_prop(SP_MISC, SPAL_STAR);
        move_sprite(SP_MISC, i, (uint8_t)(16 + 47 - bob));
        set_sprite_tile(SP_MISC + 1, SPR_HINT_UP);
        set_sprite_prop(SP_MISC + 1, SPAL_STAR | S_FLIPY);
        move_sprite(SP_MISC + 1, i, (uint8_t)(16 + 64 + bob));
        if (pressed & J_LEFT) { cur = (uint8_t)((cur + 3) & 3); redraw = 1; sfx_play(SFX_MENU); }
        if (pressed & J_RIGHT) { cur = (uint8_t)((cur + 1) & 3); redraw = 1; sfx_play(SFX_MENU); }
        if (pressed & J_UP) { dig[cur] = (uint8_t)((dig[cur] + 1) & 15); redraw = 1; sfx_play(SFX_MENU); }
        if (pressed & J_DOWN) { dig[cur] = (uint8_t)((dig[cur] + 15) & 15); redraw = 1; sfx_play(SFX_MENU); }
        if (pressed & J_B) { sfx_play(SFX_SELECT); return 0; }
        if (pressed & (J_A | J_START)) {
            v = (uint16_t)(((uint16_t)dig[0] << 12) | ((uint16_t)dig[1] << 8) | ((uint16_t)dig[2] << 4) | dig[3]);
            if (!v) { sfx_play(SFX_ERROR); continue; }
            sfx_play(SFX_SELECT);
            return v;
        }
    }
}

static void loading_screen(void)
{
    game_state = GS_GEN;
    gfx_hide_all_sprites();
    part_clear();
    clear_bkg();
    clear_win();        /* before showing it: the window still holds the last HUD/menu */
    move_win(7, 56);
    txt(1, 2, 2, "BOOTING SECTOR", PAL_UI);
    txt_num(1, 2, 3, run.sector, 4, PAL_AMBER);
    txt(1, 7, 3, run.mode == MODE_RUN ? "RUN MODE" : "ZEN MODE", PAL_CHIP);
    generate(5);
    wait_frames(10);
    move_win(7, 144);
}

void game_main(void) BANKED
{
    uint8_t choice;
    game_state = GS_BOOT;
    sound_init();
    gfx_init();
    save_load();
    rng_seed((uint16_t)(DIV_REG | 1));

    for (;;) {
        choice = title();
        if (choice == 3) { codex(); continue; }
        if (choice == 2) {
            uint16_t seed = seed_entry();
            if (!seed) continue;
            run_start(&run, MODE_RUN, seed, 1);
        } else if (choice == 0) {
            uint16_t seed = dbg_seed ? dbg_seed : (uint16_t)(seed_acc ^ ((uint16_t)DIV_REG << 8) ^ frame);
            if (!seed) seed = 0xB357;
            run_start(&run, MODE_RUN, seed, 1);
        } else {
            if (!save.zen_seed) {
                save.zen_seed = dbg_seed ? dbg_seed : (uint16_t)(seed_acc ^ ((uint16_t)DIV_REG << 8) ^ 0x2E4);
                if (!save.zen_seed) save.zen_seed = 0x2E4;
                save_write();
            }
            run_start(&run, MODE_ZEN, save.zen_seed, save.zen_sector);
        }
        cur_song = 0xFF;
        music_stop();
        loading_screen();
        play();
    }
}
