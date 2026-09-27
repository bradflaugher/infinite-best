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
2. **Time is a toy.** Hold **B** to rewind. Rewinding is free (up to 256 moves back), but the
   world remembers: your move counter and your energy never flow backwards. Rewind is
   for understanding, not for cheating par. (Braid)
3. **No wasted minutes.** Levels are small (10×8), a sector takes 20 s to 3 min, the
   next one compiles while you read your grade, and the game saves constantly. It's built
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

From sector 22 onward, sectors mix 2-4 random mechanics (3-5 from sector 80, with 2-3
chips from sector 100). Every 7th sector is a shorter "breather", and the three sectors
before it push one move harder.

The par floor rises quickly through the tutorial arc (2 at sector 1, 9 at sector 35) and then
keeps going, more slowly each time: 12 at sector 95, 13 at 245, 14 from 495 on. The cap is about
the deepest a 10×8 board gets within the Game Boy's generation budget. Typical pars:

| Sector | 10 | 20 | 30 | 40 | 60 | 100 | 150 | 250 | 500+ |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Avg par | 5.4 | 7.6 | 9.4 | 10.4 | 11.2 | 12.9 | 13.0 | 13.4 | 14.0 |

(Before the hill-climb, par flattened at ~9.8 from sector 36 on.)

## Modes

- **RUN** (the roguelike). You start with 30 energy, and each move costs 1. Clearing a sector refunds its par,
  and a BEST also pays +2 plus a streak bonus. Hints cost 3. Hitting zero ends the run.
  An optimal player gains energy every sector, so the run ends when *you* slip.
  Each run has a seed (shown on pause / game over).
- **ZEN**. No energy. Your sector number persists in the save, you can skip a sector from
  the pause menu, and hints are free.

## How the generator works (and why it always has a solution)

`src/core/gen.c` builds sectors for (seed, sector) from the unlocked mechanics. It
places wall clusters, then start, exit, chips, pads, pits, routers, gates and portals.
Then `src/core/solver.c` runs a breadth-first search over the full state space
(position × collected chips × switch state, at most 1280 states) to find par.

1. **Random rolls.** Up to 16 random boards (96 on teaching sectors, which ignore the budget
   so every lesson lands, 4 once the par floor
   reaches 11, since rolls rarely get that deep). A board that comes out too shallow first
   gets its exit moved to the stop furthest from the start (`solve_far`: a BFS with the exit
   switched off). That makes the board about as deep as its layout allows, for one extra BFS.
2. **Hill-climb.** If no roll landed, the closest one is edited one small change at a time:
   toggle a wall (sometimes across a line into the exit), move the start, move any tile,
   turn a router, and now and then move the exit to the far end again. An edit is kept when
   it doesn't move par further from the window, so the climb can wander across plateaus.
   After 20 fruitless edits it restarts from a fresh roll. Random boards top out around par
   10; the climb is what makes deep sectors deep.
3. **Budget.** Every solve is charged its visited states plus a fixed overhead
   (`gen_cost`). The search stops at `GEN_BUDGET` and keeps the closest board so far. The
   count is deterministic, so it stops at the same point on the host and the Game Boy.

- On a teaching sector, the new mechanic must *matter*. The generator strips it out
  and re-solves, and the level is rejected if the par doesn't change.
- Until sector 22, the sectors between lessons keep the newest mechanic on and ask it to
  matter too (for the first 12 rolls and 32 climb steps, which bounds the extra solving).
  Without this, the stop pads, routers, gates and portals were decoration on 40-80% of
  those boards. Pits and routers, which rarely matter by chance, get one extra tile when
  they are the focus.
- About 87% of sectors land inside their window, and the rest are usually within one move
  of it. If nothing solvable turns up at all, a simplified search runs, and a hand-made
  fallback guarantees the game can never soft-lock (it has never triggered in testing).
  `build/ibgen stats` prints all of this per sector.

The generator is deterministic. The same seed gives the same sector on the host and on
the Game Boy, and CI checks this byte-for-byte.

### Speed on a 4 MHz CPU

The BFS inner loop, one slide across the board, is hand-written SM83 assembly
(`fast_move` in `solver.c`). The portable C version next to it is the reference. The grid is
stored with a wall border (12×10) so the slide needs no bounds checks, divisions or
multiplications. One solve costs about 0.4 frames plus 0.075 frames per state visited on a
DMG, so the budget works out at about 200 frames. The budget is checked between solves and one deep solve
can overshoot it: the worst seen over 100,000 sectors was 259 frames (4.3 s), and fewer than 1 in
1,000 take over 3.8 s. Typical sectors take 0.2-1.5 s
on an original Game Boy and about half that in Game Boy Color double-speed mode. The time runs
while the clear banner is up (a "COMPILING" line with scrolling hex), and a long compile
shortens the banner's reading pause by the same amount.

## Juice

- Screen shake scales with slide distance.
- Squash on landing, and a motion trail while sliding.
- Particle bursts: chips, portals, dust against walls, stars for BEST.
- Palette flash on unlock and clear.
- A scanline wave effect while you hold rewind, and a glitch tear when you crash.
- The music detunes while you rewind and muffles in the pause menu.
- The exit animates when it comes online.
