# Release notes

Newest first. Each release is also on the repository's Releases page.

## 1.3.0 (2026-11-12)

**Arcade Foundry Cores: Classic 2D 1.3.0**

The Classic 2D cores as shipped with Insert Coin 1.3.0. The games are
unchanged since 1.2.0.

- **Game categories:** the app's library groups games by category, taken
  from each game's controls; games.toml sets one by hand where the controls
  give the wrong answer.

## 1.2.0 (2026-11-06)

**Arcade Foundry Cores: Classic 2D 1.2.0**

The Classic 2D cores as shipped with Insert Coin 1.2.0, which carries the 1.1.0
and 1.2.0 work together (there was no separate 1.1.0 release). 21 new games on
five Sega boards, 25 sets in all: the Hang-On board, the Out Run board, the X
Board, the Y Board and System 32 / System Multi 32. Each is translated ahead of
time from your own ROM set and checked against MAME, frame by frame, in attract
mode, test mode and a full game.

- **ROM check:** `check_roms` checks a ROM set with each board's importer,
  without starting the game, so the app's Downloads can say whether a set is
  complete and right.
- Debug switches: the developer environment variables are now
  `AF_DEBUG_` plus their old name (`AF_DEBUG_C2D_INTERP`); the old
  names still work in this release. The list is in Arcade-Foundry-Cores-Common's
  docs/DEBUG-SWITCHES.md.
- Build: the core interface header and the zip reader now come from the shared Arcade-Foundry-Cores-Common repo (the `common/` submodule). Clone with `--recursive`, or run `git submodule update --init`.
- **New games:** Turbo Out Run (`toutrun`, Out Run board with the FD1094), OutRunners (`orunners`, System Multi 32), Rad Rally (`radr`, System 32), Hang-On (`hangon`, the Hang-On board), Space Harrier (`sharrier`, the Hang-On board with its i8751 protection MCU), the original After Burner (`aburner131`, VER 1.31, X Board), Racing Hero (`rachero`, X Board with the FD1094), Super Monaco GP (`smgp`, X Board with the FD1094 and its second sound board), Line of Fire (`loffire`, X Board with the FD1094, two gun turrets), GP Rider (`gprider`, X Board with the FD1094, one cabinet of the twin set), Rad Mobile (`radm`, System 32), F1 Exhaust Note (`f1en`, two System 32 boards linked by shared RAM; you drive the main seat), Jurassic Park (`jpark`, System 32, two guns), SegaSonic the Hedgehog (`sonic`, System 32, trackballs and the level-load protection), Golden Axe: The Revenge of Death Adder (`ga2`, System 32 with its V25 protection CPU), Galaxy Force 2 (`gforce2`, Y Board), Power Drift (`pdrift`, Y Board), G-LOC Air Battle (`gloc`, Y Board), Strike Fighter (`strkfgtr`, Y Board), Rail Chase (`rchase`, Y Board, two guns) and Enduro Racer (`enduror`, Hang-On board with the FD1089B and the YM2151 sound board). Each matches MAME frame by frame in attract, test mode and a full game.
- **68000 timing:** an interrupt that arrives while an instruction is split across two timeslices is now taken right after that instruction, as in MAME (Hang-On's frame interrupt came about 1,000 cycles late). When a device write cuts a timeslice short, a read-modify-write instruction has already sampled its interrupt lines (its write comes after the sample), so an interrupt raised meanwhile waits one more instruction, as in MAME (Space Harrier's frame interrupt came one instruction early after 2 minutes 29 seconds of play).
- **68000 timing:** MOVE to SR and ANDI/ORI/EORI to SR sample their interrupt lines 4 cycles before their end, as MAME does (Racing Hero's timer interrupt came one instruction late after them).
- **68000 timing:** MOVEP makes each byte access at its own time, 4 cycles apart after the extension word, as MAME does; both bytes used to land at the instruction's end (Super Monaco GP releases its sound CPUs from reset with a MOVEP, so they started 8 cycles late).
- **System 32 video:** the frame in which a game switches between the 320 and 416 pixel wide modes is drawn at the old width, as in MAME (OutRunners differed in one frame at boot).
- **Recompiler (V60):** an instruction on a path no run took that does not decode is left to the dispatcher instead of stopping the build (Jurassic Park).
- **Y Board:** the new board (`src/yboard/`): three 68000s, the rotation layer and two sprite chips. MAME runs its 68000s on the Musashi core, so the recompiler has a Musashi timing mode (`m68krecomp --musashi`: Musashi's cycle table, bus accesses at the instruction's start, interrupts taken at status register writes) and the interpreter follows it. Like Musashi, the Y Board's 68000s take the address error on an odd word access (Strike Fighter relies on it).
- **FD1089:** MAME's FD1089A/B decryption (`src/fd1089/`), used by Enduro Racer's main 68000 (separate opcode and data images, as the FD1094 sets).
- **68000 timing:** MOVEM to memory makes each word write at its own time, 4 cycles apart from its first bus access, low word first for -(An), as MAME does; all its writes used to land at once (an Enduro Racer MOVEM split across two timeslices wrote its registers after the main CPU had cleared the shared RAM).
- **NEC V25:** MAME's V25 CPU (the protection CPU of Golden Axe: The Revenge of Death Adder), generated from MAME's source by `scripts/gen_v25.py`.
- **Recompiler:** a branch on a path no run took whose target does not decode as code is left to the dispatcher (and the fallback interpreter) instead of stopping the build (Line of Fire).
- **68000:** DIVS that overflows clears Z, as MAME's 68000 does (Super Monaco GP's custom IC test).
- **Recompiler:** an undefined opcode on a path no MAME run took (data after a call that never returns there, error vectors that point at text, FD1094 code decrypted in a guessed state) is compiled as the exception it decodes to instead of stopping the build.
- **Tools:** `xbd_trace`, `orun_trace` and `s32_trace` are now CMake targets in their boards' builds, as `hang_trace` is for the Hang-On board.

## 1.0.0 (2026-11-02)

**Arcade Foundry Cores: Classic 2D 1.0.0**

First release of the Classic 2D cores for Arcade Foundry: Insert Coin, as shipped with Insert Coin 1.0.0.

- **Games:** Out Run (`outrun`), Super Hang-On (`shangon`), After Burner II (`aburner2`) and Thunder Blade (`thndrbld`), from the Sega Out Run and X boards.
- **How it works:** each game's original program is translated ahead of time into native C++ and runs on a native model of its board, with no CPU emulator at play time. MAME is the reference every core is checked against, frame by frame.
- **Building:** `python3 scripts/recompile.py --set outrun --rom /path/to/outrun.zip`. Details in the README.

No ROM data is in this repository. The cores are built from your own ROM sets. BSD-3-Clause; third-party code is listed in `THIRD_PARTY.md`.

The release number is a snapshot label for "as shipped with Insert Coin 1.0.0"; each game core has its own version.
