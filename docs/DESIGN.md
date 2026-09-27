# Design notes

> *"Infinite Best": an endless puzzle that is always exactly one idea deep.*

## The pitch

You are a packet of data sliding through a circuit. When you move you **slide until
something stops you**. Reach the exit. Every sector has a **par**: the fewest possible
moves, computed by an on-device solver. Match par and you get a **BEST**.

The game is designed around three convictions, borrowed shamelessly from the
Jonathan Blow school of design:

1. **Rules, not text.** Every mechanic is introduced alone, on its own sector, with a
   one-line label. What it *means* you learn by playing. (The Witness)
2. **Time is a toy.** Hold **B** to rewind. Rewinding is free and unlimited, but the
   world remembers: your move counter and your energy never flow backwards. Rewind is
   for understanding, not for cheating par. (Braid)
3. **No wasted minutes.** Levels are small (10×8), a sector takes 20 s to 3 min, the
   next one is ready in under a second, and the game saves constantly. It's built
   for a parent with a few minutes to spare.

## Mechanics (in unlock order)

| Sector | Tile | Rule |
| ---: | --- | --- |
| 1 | Wall | Stops you. |
| 1 | Exit | Catches you as you slide over it (once it's online). |
| 3 | Stop pad | You stop *on* it. |
| 5 | Data chip | Collect all (1-3) to bring the exit online. Until then you slide over the exit. |
| 8 | Null pit | Sliding into it crashes you. The move is undone, but its energy is spent. |
| 11 | Router | Changes your slide direction. Two routers can trap you in an infinite loop ("STACK OVERFLOW"). |
| 14 | Toggle gates | Passing a switch flips which gate colour is solid, *mid-slide*. |
| 18 | Portal | Warps you to its twin, keeping your momentum. |

From sector 22 onward, sectors mix 2-4 random mechanics. Every 7th sector is a
shorter "breather". Par targets climb from ~3 to ~10 moves.

## Modes

- **RUN** (the roguelike). You start with 30 energy, and each move costs 1. Clearing a sector refunds its par,
  and a BEST also pays +2 plus a streak bonus. Hints cost 3. Hitting zero ends the run.
  An optimal player gains energy every sector, so the run ends when *you* slip.
  Each run has a seed (shown on pause / game over).
- **ZEN**. No energy. Your sector number persists in the save, you can skip a sector from
  the pause menu, and hints are free.

## How the generator works (and why it always has a solution)

`src/core/gen.c` builds a random sector for (seed, sector) from the unlocked
mechanics. It places wall clusters, then start, exit, chips, pads, pits, routers,
gates and portals. Then `src/core/solver.c` runs a breadth-first search over the full
state space (position × collected chips × switch state, at most 1280 states).

- Candidates whose par falls outside the sector's difficulty window are rejected.
- On a teaching sector, the new mechanic must *matter*. The generator strips it out
  and re-solves, and the level is rejected if the par doesn't change. Teaching sectors
  get 96 attempts instead of 48, so this holds on every seed.
- Until sector 22, the sectors between lessons keep the newest mechanic on and ask it to
  matter too (for the first 12 attempts, which bounds the extra solving). Without this,
  the stop pads, routers, gates and portals were decoration on 40-80% of those boards.
  Pits and routers, which rarely matter by chance, get one extra tile when they are the focus.
- If nothing fits after 48 attempts, the closest solvable candidate is used. A hand-made
  fallback guarantees the game can never soft-lock.

The generator is deterministic. The same seed gives the same sector on the host and on
the Game Boy, and CI checks this byte-for-byte.

### Speed on a 4 MHz CPU

The BFS inner loop, one slide across the board, is hand-written SM83 assembly
(`fast_move` in `solver.c`). The portable C version next to it is the reference. The grid is
stored with a wall border (12×10) so the slide needs no bounds checks, divisions or
multiplications. A typical sector generates in ~0.5 s on an original Game Boy and ~0.25 s
in Game Boy Color double-speed mode. The time is hidden behind the clear banner.

## Juice

- Screen shake scales with slide distance.
- Squash on landing, and a motion trail while sliding.
- Particle bursts: chips, portals, dust against walls, stars for BEST.
- Palette flash on unlock and clear.
- A scanline wave effect while you hold rewind, and a glitch tear when you crash.
- The music detunes while you rewind and muffles in the pause menu.
- The exit animates when it comes online.
