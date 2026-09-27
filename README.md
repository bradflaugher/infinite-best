# INFINITE BEST

**An endless, procedurally generated sliding puzzler for the Game Boy and Game Boy Color.**

<p align="center">
  <img src="docs/screens/title.png" width="240" alt="Title screen">
  <img src="docs/screens/play.png" width="240" alt="A sector">
  <img src="docs/screens/best.png" width="240" alt="A BEST clear">
</p>

You are a packet of data in a circuit. You slide until something stops you. Reach the exit.
Every sector has a **par**, the fewest possible moves, computed on the cartridge by a
real solver. **Match par and you get a BEST.** There's no last level. It gets deeper and it
never runs out.

It's a small, thinky game for short sessions. Each mechanic is taught wordlessly by a sector
built around it. Rewind is free (hold **B**, Braid-style), but your move counter and your
energy only go forward.

## Play it

- **On an EverDrive / real hardware** (DMG, Pocket, Color, ModRetro Chromatic, …): grab the
  `.gb` file from **Releases** and follow **[docs/EVERDRIVE.md](docs/EVERDRIVE.md)**.
- **In an emulator**: open the `.gb` file in SameBoy, mGBA, BGB, Emulicious, Gambatte…

### Controls

| Button | Action |
| --- | --- |
| D-pad | Slide |
| **B** (hold) | Rewind. Free and unlimited. Energy spent stays spent. |
| **SELECT** | Hint: shows the optimal next move and how many moves are left |
| **START** | Pause: resume / restart / hint or skip / quit |

### Modes

- **RUN**: roguelike. Each move costs 1 energy. Clearing a sector refunds its par, and a BEST
  pays a bonus that grows with your streak. How far can you get? Every run has a seed.
- **ZEN**: no energy, no pressure. Your sector is saved; skip anything you don't like.
- **CODEX**: the rules, plus a field guide to each mechanic you've discovered.

### Mechanics (in the order you meet them)

Walls → exit → **stop pads** (s3) → **data chips** that bring the exit online (s5) → **null
pits** (s8) → **routers** that bend your slide (s11) → **toggle gates** flipped by switches,
even mid-slide (s14) → **portals** that keep your momentum (s18) → from s22, everything mixes.

See **[docs/DESIGN.md](docs/DESIGN.md)** for the design notes and how the generator works.

## How it's built

The game is C, built with [GBDK-2020](https://github.com/gbdk-2020/gbdk-2020), plus hand-written SM83 assembly where it
matters.

```
src/core/     portable puzzle core (compiles with SDCC *and* gcc)
  level.*       grid, rules, slide simulation
  solver.*      BFS solver; its hot loop is hand-written SM83 asm (C reference on host)
  gen.*         deterministic generator + difficulty curve + teaching sectors
  run.*         RUN/ZEN economy (energy, par, streaks)
  rng.*         16-bit xorshift, identical on every platform
src/gb/       Game Boy front-end
  game.c        title, play loop, win/death/game-over, pause, codex   (banked)
  gfx.c         drawing, CGB palettes, shake/flash/wave FX, particles  (bank 0)
  sound.c       4-channel music + SFX engine driven from VBlank        (bank 0)
  music_data.c  songs and effects                                      (banked)
  assets.c      generated tiles, logo, palettes                        (banked)
  save.c        battery-backed SRAM records
assets/       human-editable ASCII-art tiles, palettes and music sources
tools/        gen_assets.py, gen_music.py, preview_assets.py, ibgen (host CLI)
tests/        host unit tests + emulator end-to-end tests
```

The cartridge is **MBC5 + RAM + battery**, 64 KB, CGB-compatible (colour and double speed on
Color hardware, still fully playable on a 1989 brick).

### Build

Requirements: GBDK-2020 4.3 (at `/opt/gbdk`, or set `GBDK_HOME`), gcc, Python 3,
and `pip install pyboy pillow` for the emulator tests.

```sh
make rom          # -> build/infinite-best.gb (+ .sym)
make test         # everything below
make test-host    # C unit tests: rules, solver, generator, economy, sound engine
make test-assets  # asset pipeline tests
make test-rom     # boots the ROM in PyBoy (DMG and CGB) and plays it
```

`build/ibgen` is a host CLI for the puzzle core:

```sh
build/ibgen show 42 30     # ASCII render of seed 42, sector 30
build/ibgen solve 42 30    # optimal solution, e.g. "9 DLLDLDLUR"
build/ibgen stats 100 100  # generator statistics
```

### Tests

- **Core (C, host).** Every rule and edge case: slides, chips, stop pads, pits, arrow loops,
  gates toggling mid-slide, portals, unsolvable detection and hints. The tests also generate
  ~1,500 sectors and replay the solver's hint chain on each one, checking that it reaches the
  exit in exactly `par` moves. They check determinism, the teaching sectors, the difficulty
  curve, the RUN/ZEN economy, and that the RNG has its full period.
- **Sound (C, host).** Plays every song and effect against a fake APU and checks the register
  writes.
- **ROM (PyBoy, DMG + CGB).**
  - Boots to the title.
  - Starts a seeded run and checks that the level the **Game Boy generated is byte-identical
    to the host build**. This validates the SM83 assembly against the C reference.
  - Plays six sectors with the host's optimal solutions and checks that every clear is a BEST.
  - Tests rewind (state restored, energy not refunded), the pause menu, running out of energy,
    the game-over screen and the codex.
  - Saves screenshots as CI artifacts.

### CI / releases

GitHub Actions ([`.github/workflows/ci.yml`](.github/workflows/ci.yml)) installs GBDK, runs
all tests and uploads the ROM and screenshots. On every push to the default branch that passes,
it publishes a release **tagged with the date** (`YYYY.MM.DD`) and **deletes every older
release**, so there is only ever one: the latest.

## License

MIT, see [LICENSE](LICENSE).
