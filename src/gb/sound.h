/* sound.h - INFINITE BEST sound driver (music + sfx) for GBDK-2020.
 *
 * Channel use:  pulse 1 = lead, pulse 2 = arps/harmony, wave = bass,
 *               noise = drums.  SFX temporarily steal pulse 1 / pulse 2 /
 *               noise (never the wave channel); music keeps running silently
 *               on a stolen channel and resumes cleanly when the sfx ends.
 *
 * Threading: every public call only posts a request (single-byte writes /
 * a 4-entry lock-free queue); all register work happens inside sound_tick(),
 * so it is safe to call these from the main loop while sound_tick() runs in
 * the VBL interrupt.  Requests take effect on the next sound_tick().
 */
#ifndef SOUND_H
#define SOUND_H

#include <stdint.h>

enum { SONG_TITLE, SONG_RUN_A, SONG_RUN_B, SONG_ZEN, SONG_GAMEOVER, NUM_SONGS };
enum { SFX_MOVE, SFX_BUMP, SFX_CHIP, SFX_SWITCH, SFX_PORTAL, SFX_ARROW, SFX_PIT,
       SFX_REWIND, SFX_MENU, SFX_SELECT, SFX_WIN, SFX_BEST, SFX_UNLOCK, SFX_GEN,
       SFX_NEWMECH, SFX_HINT, SFX_ERROR, NUM_SFX };

void sound_init(void);            /* enable APU (NR52=0x80, NR50=0x77, NR51=0xFF), init state */
void sound_tick(void);            /* call exactly once per frame (60Hz), e.g. from VBL */
void music_play(uint8_t song);    /* start song from the beginning (restarts if same) */
void music_stop(void);
uint8_t music_playing(void);      /* 0 if stopped / non-looping song finished */
void sfx_play(uint8_t sfx);       /* play sfx (steals its channels from music) */
void music_set_rewind(uint8_t on);/* time-rewind effect: detune/warble down + slow down */
void music_set_muffle(uint8_t on);/* pause effect: drop lead+drums, lower master volume */
void sound_mute_all(uint8_t on);  /* global mute (music and sfx keep running silently) */

/* --- introspection helpers (cheap; used by tests / debug) --- */
uint8_t sound_debug_loops(void);  /* times the current song wrapped to its loop point */
uint8_t sound_debug_owned(void);  /* channel mask currently owned by sfx (bit0=p1,1=p2,3=noise) */
uint8_t sfx_playing(void);        /* nonzero while any sfx is playing / pending */

#endif
