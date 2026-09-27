/* test_sound.c - host-side unit tests for the INFINITE BEST sound driver.
 *
 * Build + run (from the repo root):
 *   gcc -std=c99 -Wall -Wextra -DHOST_TEST -Isrc/gb tests/test_sound.c \
 *       src/gb/sound.c src/gb/music_data.c -o build/test_sound && ./build/test_sound
 *
 * Every register write goes through host_snd_write() (see hw_sound.h); the
 * hook below validates each one and keeps per-test statistics.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "sound.h"
#include "music_data.h"
#include "hw_sound.h"

static int n_pass, n_fail;
#define CHECK(cond, ...) do { if (cond) n_pass++; else { n_fail++; \
    printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ---- write monitor ---- */
static unsigned long w_total, w_bad_reg, w_bad_nrx4, w_bad_wave, w_bad_52;
static unsigned long w_music_on_owned;      /* music wrote to a channel owned by sfx */
static unsigned long w_sfx_outside;         /* sfx wrote outside its declared mask */
static unsigned long trig[4], trig_music[4];
static unsigned long ch_writes[4];
static uint8_t sfx_mask_now;                /* union of masks of sfx being tested */

static int reg_channel(uint8_t r)
{
    if (r >= 0x10 && r <= 0x14) return 0;
    if (r >= 0x16 && r <= 0x19) return 1;
    if ((r >= 0x1A && r <= 0x1E) || (r >= 0x30 && r <= 0x3F)) return 2;
    if (r >= 0x20 && r <= 0x23) return 3;
    return -1;
}

static void hook(uint8_t r, uint8_t v)
{
    int c = reg_channel(r);
    w_total++;
    if (!((r >= 0x10 && r <= 0x26 && r != 0x15 && r != 0x1F) || (r >= 0x30 && r <= 0x3F)))
        w_bad_reg++;
    if ((r == 0x14 || r == 0x19 || r == 0x1E || r == 0x23) && (v & 0x38))
        w_bad_nrx4++;                        /* 11-bit frequency overflow / junk bits */
    if (r == 0x23 && (v & 0x3F))
        w_bad_nrx4++;
    if (r >= 0x30 && r <= 0x3F && (host_snd_regs[0x1A] & 0x80))
        w_bad_wave++;                        /* wave RAM written with ch3 DAC on */
    if (r == 0x26 && v != 0x80)
        w_bad_52++;
    if (c >= 0) {
        ch_writes[c]++;
        if ((r == 0x14 || r == 0x19 || r == 0x1E || r == 0x23) && (v & 0x80)) {
            trig[c]++;
            if (host_snd_src == SND_SRC_MUSIC)
                trig_music[c]++;
        }
        if (host_snd_src == SND_SRC_MUSIC && (sound_debug_owned() & (1 << c)))
            w_music_on_owned++;
        if (host_snd_src == SND_SRC_SFX && !(sfx_mask_now & (1 << c)))
            w_sfx_outside++;
    }
    if (host_snd_src == SND_SRC_SFX && c < 0)
        w_sfx_outside++;
}

static void reset_stats(void)
{
    w_total = w_bad_reg = w_bad_nrx4 = w_bad_wave = w_bad_52 = 0;
    w_music_on_owned = w_sfx_outside = 0;
    memset(trig, 0, sizeof trig);
    memset(trig_music, 0, sizeof trig_music);
    memset(ch_writes, 0, sizeof ch_writes);
}

static void check_clean(const char *what)
{
    CHECK(w_bad_reg == 0, "%s: %lu writes to invalid registers", what, w_bad_reg);
    CHECK(w_bad_nrx4 == 0, "%s: %lu bad NRx4 values (freq > 11 bit)", what, w_bad_nrx4);
    CHECK(w_bad_wave == 0, "%s: %lu wave RAM writes with DAC on", what, w_bad_wave);
    CHECK(w_bad_52 == 0, "%s: %lu bad NR52 writes", what, w_bad_52);
    CHECK(w_music_on_owned == 0, "%s: %lu music writes to sfx-owned channels", what, w_music_on_owned);
    CHECK(w_sfx_outside == 0, "%s: %lu sfx writes outside mask", what, w_sfx_outside);
}

static void ticks(int n)
{
    while (n-- > 0)
        sound_tick();
}

static void fresh(void)
{
    memset(host_snd_regs, 0, sizeof host_snd_regs);
    sound_init();
    reset_stats();
}

static const char *song_names[NUM_SONGS] = { "TITLE", "RUN_A", "RUN_B", "ZEN", "GAMEOVER" };

static void test_init(void)
{
    host_snd_hook = hook;
    fresh();
    sound_init();
    CHECK(host_snd_regs[0x26] == 0x80, "NR52 on");
    CHECK(host_snd_regs[0x24] == 0x77, "NR50 = 0x77");
    CHECK(host_snd_regs[0x25] == 0xFF, "NR51 = 0xFF");
    CHECK(!music_playing(), "nothing playing after init");
    CHECK(!sfx_playing(), "no sfx after init");
    ticks(10);
    check_clean("idle");
}

static void test_songs(void)
{
    uint8_t s;
    for (s = 0; s < NUM_SONGS; s++) {
        int f, stopped_at = -1;
        unsigned long after;
        fresh();
        music_play(s);
        CHECK(music_playing(), "%s: playing right after music_play", song_names[s]);
        for (f = 0; f < 5000; f++) {
            sound_tick();
            if (stopped_at < 0 && !music_playing())
                stopped_at = f;
        }
        check_clean(song_names[s]);
        CHECK(trig[0] && trig[1] && trig[2] && trig[3],
              "%s: all 4 channels used (%lu %lu %lu %lu)", song_names[s], trig[0], trig[1], trig[2], trig[3]);
        if (snd_song_loops[s]) {
            CHECK(stopped_at < 0, "%s: looping song stopped at %d", song_names[s], stopped_at);
            CHECK(sound_debug_loops() >= 1, "%s: song looped (%u)", song_names[s], sound_debug_loops());
            CHECK(snd_song_frames[s] < 5000, "%s: loop fits test window", song_names[s]);
        } else {
            CHECK(stopped_at > 0, "%s: one-shot song stopped", song_names[s]);
            CHECK(stopped_at < 400, "%s: one-shot song short (%d frames)", song_names[s], stopped_at);
            CHECK(sound_debug_loops() == 0, "%s: one-shot did not loop", song_names[s]);
            after = w_total;
            ticks(200);
            CHECK(w_total == after, "%s: no writes after end (%lu)", song_names[s], w_total - after);
            CHECK(host_snd_regs[0x12] == 0 && host_snd_regs[0x17] == 0 && !(host_snd_regs[0x1A] & 0x80),
                  "%s: channels silenced at end", song_names[s]);
        }
    }
}

static int run_sfx_until_done(int limit)
{
    int f = 0;
    while (f < limit) {
        sound_tick();
        f++;
        if (!sfx_playing())
            break;
    }
    return f;
}

static void test_sfx(void)
{
    uint8_t i, with_music;
    for (with_music = 0; with_music < 2; with_music++) {
        for (i = 0; i < NUM_SFX; i++) {
            int f;
            char what[48];
            fresh();
            if (with_music) {
                music_play(SONG_RUN_A);
                ticks(37);
            }
            reset_stats();
            sfx_mask_now = snd_sfx[i].mask;
            sfx_play(i);
            CHECK(sfx_playing(), "sfx %u pending", i);
            f = run_sfx_until_done(600);
            sprintf(what, "sfx %u%s", i, with_music ? " +music" : "");
            CHECK(f < 200, "%s: finished in %d frames", what, f);
            CHECK(f >= snd_sfx_frames[i], "%s: ran full length (%d < %u)", what, f, snd_sfx_frames[i]);
            CHECK(sound_debug_owned() == 0, "%s: channels released", what);
            CHECK(host_snd_regs[0x10] == 0 || !(snd_sfx[i].mask & 1), "%s: sweep cleared", what);
            check_clean(what);
            sfx_mask_now = 0;
            if (!with_music) {
                /* nothing may keep sounding on the sfx channels */
                if (snd_sfx[i].mask & 1) CHECK(host_snd_regs[0x12] == 0, "%s: p1 silent after", what);
                if (snd_sfx[i].mask & 2) CHECK(host_snd_regs[0x17] == 0, "%s: p2 silent after", what);
                if (snd_sfx[i].mask & 8) CHECK(host_snd_regs[0x21] == 0, "%s: noise silent after", what);
            }
        }
    }
}

static void test_resume(void)
{
    uint8_t s;
    uint8_t fx[3] = { SFX_CHIP, SFX_BEST, SFX_HINT };
    for (s = 0; s < 3; s++) {
        uint8_t m = snd_sfx[fx[s]].mask, c;
        int f;
        fresh();
        music_play(SONG_TITLE);
        ticks(600);                          /* into the loop: all channels busy */
        sfx_mask_now = m;
        sfx_play(fx[s]);
        run_sfx_until_done(600);
        sfx_mask_now = 0;
        reset_stats();
        for (f = 0; f < 300; f++)
            sound_tick();
        for (c = 0; c < 4; c++)
            if (m & (1 << c))
                CHECK(trig_music[c] > 0, "resume: music retriggers ch%u after sfx %u", c, fx[s]);
        check_clean("resume");
    }
    /* sfx priorities / overlap: WIN must not be cut by MOVE, BEST beats WIN */
    fresh();
    music_play(SONG_RUN_A);
    ticks(5);
    sfx_mask_now = 0x0B;
    sfx_play(SFX_WIN);
    ticks(3);
    sfx_play(SFX_MOVE);
    ticks(1);
    CHECK(sound_debug_owned() == 0x03, "WIN keeps p1+p2 against MOVE (owned %02x)", sound_debug_owned());
    sfx_play(SFX_BEST);
    ticks(1);
    CHECK(sound_debug_owned() == 0x0B, "BEST takes p1+p2+noise (owned %02x)", sound_debug_owned());
    run_sfx_until_done(600);
    CHECK(sound_debug_owned() == 0, "all released");
    /* two voices in parallel: CHIP (p1) + UNLOCK (p2) */
    sfx_play(SFX_CHIP);
    sfx_play(SFX_UNLOCK);
    ticks(2);
    CHECK(sound_debug_owned() == 0x03, "CHIP+UNLOCK layered (owned %02x)", sound_debug_owned());
    run_sfx_until_done(600);
    CHECK(sound_debug_owned() == 0, "layered released");
    /* GEN retriggered every other frame */
    {
        int k;
        for (k = 0; k < 60; k++) { sfx_play(SFX_GEN); ticks(2); }
        CHECK(run_sfx_until_done(100) < 10, "GEN spam settles");
    }
    sfx_mask_now = 0;
    check_clean("priorities");
}

static void test_stop(void)
{
    unsigned long before;
    fresh();
    music_play(SONG_RUN_B);
    ticks(500);
    music_stop();
    CHECK(!music_playing(), "music_playing()==0 right after stop");
    sound_tick();
    CHECK(host_snd_regs[0x12] == 0, "stop: NR12 silent");
    CHECK(host_snd_regs[0x17] == 0, "stop: NR22 silent");
    CHECK(!(host_snd_regs[0x1A] & 0x80), "stop: NR30 DAC off");
    CHECK(host_snd_regs[0x21] == 0, "stop: NR42 silent");
    before = w_total;
    ticks(300);
    CHECK(w_total == before, "stop: no writes while stopped (%lu)", w_total - before);
    /* stop while an sfx owns p1: sfx finishes, channel ends silent */
    music_play(SONG_TITLE);
    ticks(300);
    sfx_mask_now = snd_sfx[SFX_PORTAL].mask;
    sfx_play(SFX_PORTAL);
    ticks(3);
    music_stop();
    run_sfx_until_done(600);
    sfx_mask_now = 0;
    CHECK(host_snd_regs[0x12] == 0, "stop during sfx: p1 silent after sfx");
    /* restart works */
    music_play(SONG_TITLE);
    ticks(2);
    CHECK(music_playing(), "restart after stop");
    CHECK(trig[1] > 0 || trig[2] > 0, "restart triggers notes");
    check_clean("stop");
}

static uint16_t reg_freq(uint8_t lo_reg)
{
    return (uint16_t)(host_snd_regs[lo_reg] | ((host_snd_regs[lo_reg + 1] & 7) << 8));
}

static int in_table(uint16_t f)
{
    int i;
    for (i = 0; i < SND_NUM_NOTES; i++)
        if (snd_freq[i] == f)
            return 1;
    return 0;
}

static void test_effects(void)
{
    int f, lower = 0, total = 0;
    uint8_t s;
    for (s = 0; s < NUM_SONGS; s++) {
        fresh();
        music_play(s);
        ticks(200);
        music_set_rewind(1);
        for (f = 0; f < 400; f++) {
            sound_tick();
            sfx_mask_now = 0x0B;
            if (s != SONG_RUN_A && (f & 31) == 5)
                sfx_play((uint8_t)(f % NUM_SFX));   /* sfx during rewind must not break */
            if (s == SONG_RUN_A && host_snd_regs[0x17]) {
                total++;
                if (!in_table(reg_freq(0x18)))
                    lower++;
            }
        }
        music_set_rewind(0);
        run_sfx_until_done(600);
        sfx_mask_now = 0;
        check_clean("rewind");
        if (s == SONG_RUN_A)
            CHECK(lower > total / 2, "rewind detunes p2 (%d of %d frames off-table)", lower, total);
    }
    /* after rewind off, the (vibrato-free) arp channel is back on the table */
    fresh();
    music_play(SONG_RUN_A);
    ticks(100);
    music_set_rewind(1);
    ticks(300);
    music_set_rewind(0);
    ticks(2);
    CHECK(in_table(reg_freq(0x18)), "rewind off restores p2 pitch (%u)", reg_freq(0x18));
    CHECK(in_table(reg_freq(0x1D)), "rewind off restores wave pitch (%u)", reg_freq(0x1D));
    {   /* tempo slows while rewinding: count p2 triggers */
        unsigned long a, b;
        reset_stats(); ticks(600); a = trig[1];
        music_set_rewind(1); reset_stats(); ticks(600); b = trig[1];
        music_set_rewind(0);
        CHECK(b < a, "rewind slows tempo (%lu vs %lu triggers)", b, a);
    }

    /* muffle */
    fresh();
    music_play(SONG_TITLE);
    ticks(100);
    music_set_muffle(1);
    sound_tick();
    CHECK((host_snd_regs[0x25] & 0x99) == 0, "muffle drops lead+drums (NR51 %02x)", host_snd_regs[0x25]);
    CHECK((host_snd_regs[0x25] & 0x66) == 0x66, "muffle keeps arps+bass (NR51 %02x)", host_snd_regs[0x25]);
    CHECK(host_snd_regs[0x24] < 0x77, "muffle lowers NR50");
    sfx_mask_now = 1;
    sfx_play(SFX_MENU);
    sound_tick();
    CHECK((host_snd_regs[0x25] & 0x11) == 0x11, "menu sfx audible while muffled (NR51 %02x)", host_snd_regs[0x25]);
    run_sfx_until_done(100);
    sfx_mask_now = 0;
    CHECK((host_snd_regs[0x25] & 0x11) == 0, "p1 muffled again after sfx (NR51 %02x)", host_snd_regs[0x25]);
    ticks(100);
    music_set_muffle(0);
    sound_tick();
    CHECK(host_snd_regs[0x25] == 0xFF && host_snd_regs[0x24] == 0x77, "unmuffle restores mix");

    /* mute */
    sound_mute_all(1);
    sound_tick();
    CHECK(host_snd_regs[0x25] == 0x00, "mute: NR51 = 0");
    ticks(50);
    CHECK(music_playing(), "music keeps running while muted");
    sound_mute_all(0);
    sound_tick();
    CHECK(host_snd_regs[0x25] == 0xFF, "unmute restores NR51");
    check_clean("muffle/mute");
}

static void test_stress(void)
{
    int f;
    unsigned int seed = 12345;
    fresh();
    for (f = 0; f < 40000; f++) {
        unsigned int r;
        seed = seed * 1103515245u + 12345u;
        r = (seed >> 16) & 0x7FFF;
        if (r % 97 == 0) music_play((uint8_t)(r % (NUM_SONGS + 2)));  /* includes invalid ids */
        if (r % 13 == 0) sfx_play((uint8_t)(r % (NUM_SFX + 3)));
        if (r % 211 == 0) music_set_rewind((uint8_t)(r & 1));
        if (r % 307 == 0) music_set_muffle((uint8_t)((r >> 1) & 1));
        if (r % 509 == 0) sound_mute_all((uint8_t)((r >> 2) & 1));
        if (r % 1009 == 0) music_stop();
        sfx_mask_now = 0x0B;               /* any sfx channel (never wave) */
        sound_tick();
    }
    sfx_mask_now = 0;
    CHECK(w_bad_reg == 0 && w_bad_nrx4 == 0 && w_bad_wave == 0, "stress: register rules hold");
    CHECK(w_music_on_owned == 0, "stress: music never writes owned channels");
    CHECK(w_sfx_outside == 0, "stress: sfx never touch wave channel");
    music_set_rewind(0);
    music_set_muffle(0);
    sound_mute_all(0);
    CHECK(run_sfx_until_done(600) < 600, "stress: sfx drain");
}

int main(void)
{
    host_snd_hook = hook;
    test_init();
    test_songs();
    test_sfx();
    test_resume();
    test_stop();
    test_effects();
    test_stress();
    printf("test_sound: %d passed, %d failed\n", n_pass, n_fail);
    return n_fail ? 1 : 0;
}
