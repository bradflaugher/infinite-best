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
chips from sector 100), and one of them (not chips) is the board's focus: the search asks
the solution to use it on the first few rolls, then for it to at least change par, until half
the budget is spent. Every 7th sector is a shorter "breather" built around one mechanic
(gates only when there's nothing else), sometimes with chips, and the three sectors before
it push one move harder. Without a focus, the clean-up pass (below) stripped every special
tile off about a quarter of the boards in sectors 22-35 and up to 80% of the breathers,
leaving plain wall mazes; now about 10% of sectors 22-35 are walls and chips only.

The par floor rises quickly through the tutorial arc (2 at sector 1, 9 at sector 35) and then
keeps going, more slowly each time: 12 at sector 95, 13 at 245, 14 from 495 on. The cap is about
the deepest a 10×8 board gets within the Game Boy's generation budget. Typical pars:

| Sector | 10 | 20 | 30 | 40 | 60 | 100 | 150 | 250 | 500+ |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Avg par | 5.0 | 7.2 | 9.2 | 10.8 | 11.6 | 12.7 | 12.5 | 13.1 | 14.0 |

(Before the hill-climb, par flattened at ~9.8 from sector 36 on.)

## Modes

- **RUN** (the roguelike). You start with 40 energy (the tank holds 60), and each move costs 1. Clearing a
  sector refunds its par, and a BEST also pays +1 (+2 on a 3-BEST streak, +3 on a 6-streak). Hints cost 3 and break the BEST streak
  (asking again while the arrow is still up, or getting no answer, is free). Hitting zero ends the run, and so does starting a sector with less energy than its par
  (it can't be won: rewinds never give energy back), with a PAR > ENERGY message.
  An optimal player gains energy every sector, so the run ends when *you* slip. The small tank
  is deliberate: with the old 99 cap and +2-plus-streak bonus, a player who BESTs most sectors
  banked so much energy that mistakes stopped mattering and runs never ended. In a simple
  player model (`median sector reached`), a 50% BEST rate now gets to about 19, 70% to about 32,
  85% to about 60 and 95% to about 100+.
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
4. **Clean-up.** Each pad, pit, router, switch, gate and portal pair is taken off in turn,
   and stays off if par is unchanged (and the focus mechanic keeps its role). This costs one
   solve per tile, capped at `GEN_TIDY_COST` and `GEN_TIDY_BUDGET`. Before it, about 70% of
   special tiles were clutter that changed nothing; after it 8-18% on sectors 22-100 and
   30% deeper in, where the climb has often used up the budget.

- On a teaching sector, the new mechanic must be *used*. The generator strips it out and
  re-solves, and keeps the level only if that makes it longer or unsolvable. Pads, routers
  and portals aren't solid, so then every optimal solution goes through them. The gate
  lesson has to need its switch (strip just the switches), the chip lesson a detour (open
  the exit from the start), and pits, which can only get in the way, must change par. Before
  this, the solution ignored the new tile on 40-60% of lessons and 90% of gate lessons
  never needed the switch: the new tile was only an obstacle. Once the budget is spent a
  lesson settles for a mechanic that changes par.
- Until sector 22, the sectors between lessons keep the newest mechanic on and ask the
  solution to use it too (for the first 12 rolls and 32 climb steps), then settle for it
  changing par. Without this, the stop pads, routers, gates and portals were decoration on
  40-80% of those boards. Pits and routers, which rarely matter by chance, get one extra
  tile when they are the focus. On a pit lesson one pit sits beside the exit and one a few
  cells out from the start. On a gate lesson a gate guards the exit.
- A router never points straight into a wall (that is just a stop pad in disguise).
- About 83% of sectors land inside their window (nearly all early ones, fewer deep in), and the rest are usually within one move
  of it. If nothing solvable turns up at all, a simplified search runs, and a hand-made
  fallback guarantees the game can never soft-lock (it has never triggered in testing).
  `build/ibgen stats` prints all of this per sector.

The generator is deterministic. The same seed gives the same sector on the host and on
the Game Boy, and CI checks this byte-for-byte.

### Measuring puzzle quality

Par is a weak proxy for a good puzzle, so `build/ibgen quality <seeds> <sectors>` explores
each board's whole state graph on the host (`tools/quality.h`) and averages, per sector band:
how many first moves are optimal, how many distinct optimal solutions there are, the chance
that random play stumbles into a BEST, whether a naive player who always slides towards the
nearest chip or the exit gets par, dead-end states, and which special tiles and walls are
idle (removing one leaves par unchanged). `build/ibgen q <seed> <sector>` shows one board
with its numbers. Over 30 seeds:

| Sectors | 3-10 | 11-21 | 22-35 | 36-100 | 101-300 |
| --- | --- | --- | --- | --- | --- |
| Single optimal first move | 92% | 92% | 89% | 90% | 87% |
| Optimal solutions (avg) | 1.1 | 1.2 | 1.3 | 1.4 | 1.6 |
| Naive play gets par | 57% | 26% | 19% | 11% | 8% |
| Idle special tiles, before clean-up | 77% | 76% | 74% | 66% | 64% |
| Idle special tiles, after | 29% | 30% | 7% | 14% | 24% |

Most boards already have one way in and one solution, so the lever that mattered was
clutter and lessons. Removing idle *walls* as well was tried and dropped: boards got
barren, and boards with a single optimal first move fell from about 90% to 77-86%.
Rejecting boards the naive player solves was tried too: it barely moved the numbers
(55% to 47% on sectors 3-10) for extra solving, since early pars are only 2-5 moves.

### Speed on a 4 MHz CPU

The BFS inner loop, one slide across the board, is hand-written SM83 assembly
(`fast_move` in `solver.c`). The portable C version next to it is the reference. The grid is
stored with a wall border (12×10) so the slide needs no bounds checks, divisions or
multiplications. Measured in the ROM (`dbg_gen_frames`, 2,400 sectors over 8 seeds), one solve
costs about 0.64 frames plus 0.0735 frames per state visited on a DMG, so the per-solve work
outside the BFS is worth about 9 visited states, and that is what `GEN_SOLVE_OVERHEAD` charges
(it used to charge 5, which let about 1 sector in 20 run past 4 s; `ibgen stats` uses the
measured fit). The budget is checked between solves and one deep solve can overshoot it: the
worst measured was 269 frames (4.5 s), and 0.7% take over 4 s. Typical sectors take 0.9-3.5 s
(median 2.2 s, the first 50 sectors average about 1.5 s) on an original Game Boy and about half
that in Game Boy Color double-speed mode (median 1.05 s, worst 2.2 s). The time runs
while the clear banner is up, under a "COMPILING" progress bar. The bar is drawn from the VBlank
interrupt, so it keeps filling (and a highlight keeps running along it) even while the CPU is
deep inside one solve. A long compile
shortens the banner's reading pause by the same amount.

## Juice

- Screen shake scales with slide distance.
- Squash on landing, and a motion trail while sliding.
- Particle bursts: chips, portals, dust against walls, stars for BEST.
- Palette flash on unlock and clear.
- A scanline wave effect while you hold rewind, and a glitch tear when you crash.
- The music detunes while you rewind and muffles in the pause menu.
- The exit animates when it comes online.
