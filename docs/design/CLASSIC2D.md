# Classic 2D cores

How the Classic 2D cores were planned and built: Sega's sprite-scaling and
pre-3D boards, from Hang-On (1985) to the System 32 and Multi 32 games of the
early 1990s. The dated history is in [HANDOFF.md](../../HANDOFF.md) (October
2026 in [handoff/2026-10.md](../../handoff/2026-10.md)), the current state in
[STATE.md](../../STATE.md) and the rules in `rules.md`. The 68000 recompiler
and its timing model have their own document, [M68000.md](M68000.md).

## Purpose and scope

Play Sega's classic 2D arcade games natively in the app, on the same terms
as the 3D boards: the user supplies their own MAME ROM sets, the build turns
each set into a core, and the app plays it with a gamepad, a wheel, a flight
stick or a light gun setup.

Five boards and 26 sets are built:

| Board | Sets | CPUs |
|---|---|---|
| Hang-On board | hangon, sharrier, enduror | two 68000s, Z80; i8751 MCU (sharrier); FD1089B (enduror) |
| Out Run board | outrun, shangon, toutrun | two 68000s, Z80; FD1094 (toutrun) |
| X Board | aburner2, aburner131, thndrbld, thndrbld1, rachero, smgp, loffire, gprider | two 68000s, Z80; FD1094 on several |
| Y Board | gforce2, pdrift, gloc, strkfgtr, rchase | three 68000s, Z80 |
| System 32 / Multi 32 | orunners, radr, radm, f1en, jpark, sonic, ga2 | V60 or V70, Z80; V25 (ga2) |

In scope: attract, play, test mode, sound, the operator settings and
high scores (the board's own NVRAM or EEPROM), and the app's library, Add
ROM and controls, on every platform the app runs on.

Planned: more sets on these boards, the Turbo board ([TURBO.md](TURBO.md),
#31), Multi 32's second screen, and link play for the linked cabinets.

## Requirements

1. Nothing from a ROM is in the repository: no program image, no generated
   C++, no samples or graphics (`rules.md` 6 and 7). The program is
   translated on the user's machine at build time.
2. Every set behaves as it does in MAME, which is the reference: attract,
   test mode and a full game to game over, checked CPU by CPU and frame by
   frame, with the input replays committed.
3. One core per set behind the shared `afcore.h` interface (from Common), so
   the app handles a Classic 2D game like any other.
4. The app's existing controls work unchanged: drive (wheel, pedals, gear),
   flight (stick, throttle, buttons), guns and turrets, sticks and buttons.
5. The user's MAME set is used as it comes: a `.zip` or a folder of its
   files, every file checked by CRC before anything is built.

## How it was planned

The boards were chosen for how much they could reuse. The 68000 and Z80
already ran as recompiled code in the Model 2 sound board, and the V60
recompiler from the Model 1 cores covers System 32's CPUs, so the work was
mostly board models, video and sound.

The approach is the one the Model 1 and Model 2 cores proved:

- **Recompile ahead of time.** Each program is translated into C++ before
  play: `tools/m68krecomp` for the 68000s, `tools/v60recomp` for the V60
  and V70. The Z80, i8751 and V25 are small and run as MAME's own cores,
  generated from MAME's sources at build time.
- **Model the board natively.** Memory maps, custom chips, timers and
  video come from MAME's drivers (BSD-3-Clause), transplanted into plain C++
  without MAME's device framework.
- **Run MAME's scheduler.** `src/sched` is MAME's scheduler, so the CPUs
  interleave and the timers fire exactly as in MAME. That is what makes a
  frame-by-frame comparison possible at all.
- **Use MAME as the oracle.** A patched MAME build per board writes
  register snapshots, interrupts and a hash of every screen update. The
  native trace tool writes the same lines, and the two logs are compared
  line by line.

The order was one board at a time, each game through the same steps: the
CPUs match in attract, then the video, then test mode and a full game with
replays, then the core library and the app entry. Out Run went first, then
the X Board, the Out Run board's other sets, System 32, the Hang-On board
and the Y Board.

## Design

### Overview

```
MAME set (.zip)
  │
  ├─ build time: c2d_import ── checks CRCs, writes program images
  │              m68krecomp / v60recomp ── C++ for each program (seeded)
  │
  └─ run time: afcore_<set>
        generated code  ⇄  board model (maps, custom chips, timers)
                            MAME's scheduler (src/sched)
                            Z80 / i8751 / V25 (MAME's cores, generated)
                            video → indexed bitmap → RGB
                            sound chips (ymfm, SegaPCM, MultiPCM, RF5C68)
                            inputs ← afcore inputs
```

### Repository layout

| Path | What |
|---|---|
| `src/m68k`, `src/cpu68k` | 68000 decoder, timing tables, CPU state, the fallback interpreter ([M68000.md](M68000.md)) |
| `src/v60` | V60/V70 handlers (MAME's), the native environment the generated code runs against, a reference interpreter |
| `src/z80` | MAME's Z80 step machine, generated from `z80.lst` |
| `src/sched` | MAME's scheduler and attotime |
| `src/runtime` | Shared devices: 8255, 93C46 EEPROM, MCS-51, MultiPCM, RF5C68, the V25, ROM loading |
| `src/fd1094`, `src/fd1089` | The encrypted 68000s' decryption (MAME's) |
| `src/hangon`, `src/outrun`, `src/xboard`, `src/yboard`, `src/system32` | One board each: `board`, `rom_import`, `core.cpp`; Out Run's `video`, `sound` and `screen` are shared by the Hang-On board, the X Board and the Y Board |
| `tools/` | `m68krecomp`, `v60recomp`, `c2d_import`, `afcore_run` and a trace tool per board |
| `seeds/` | Per set: program addresses MAME executed (our own analysis, no ROM bytes) |
| `tests/replays/` | Per set: attract, test mode and game input replays |
| `patches/mame/` | The trace patches for the oracle builds |

### Building a core

`scripts/recompile.py --set SET --rom ROMSET` does the whole build:

1. `c2d_import` reads the set (zip or folder), checks every file against
   MAME's ROM list with CRCs, and writes the program images the recompilers
   read: de-interleaved, and for the FD1094 and FD1089 sets also the
   decrypted opcode image.
2. `m68krecomp` translates each 68000 program, seeded with `seeds/<set>.*`
   plus what it finds itself: jump tables, handler pointers and code
   addresses in data, each kept only if everything reachable from it decodes
   cleanly. System 32 sets go through `v60recomp` instead.
3. CMake builds `afcore_<set>`: the generated code, the board and the
   shared libraries. On Apple platforms it is a framework.

Cross builds (iOS, tvOS, arm64 Linux) run `--gen-only` on the host and
configure CMake for the target with `-DC2D_GEN_ROOT`. Everything the build
writes stays under `build/`, which git ignores.

`games.toml` lists every set: its title, year, board, controls and core
version. `scripts/gen_games.py` turns it into the manifest the app reads
(`--check` in CI). A new board also needs its id in Common's `afgames.py`.

### CPUs

- **68000.** The recompiled code runs whole instructions, with MAME's cycle
  counts, bus access times and interrupt sampling points. Code no MAME run
  reached goes to a fallback interpreter that uses the same decoder and
  timing. The Y Board's 68000s use Musashi's timing instead, because MAME
  runs them on Musashi. All of this is in [M68000.md](M68000.md).
- **V60 and V70.** `v60recomp` (from the Model 1 cores' recompiler) emits
  C++ that calls MAME's instruction handlers with the decoding fixed per
  instruction. MAME counts 8 cycles per instruction, so a timeslice is a
  number of instructions. The V60 has no fallback interpreter yet: a path no
  run reached stops the core with an error naming the address.
- **Z80, i8751, V25.** These run as MAME's own cores, generated from
  MAME's source at build time (`src/z80`, `scripts/gen_mcs51.py`,
  `scripts/gen_v25.py`) with shims for memory, timers and callbacks.
- **Encrypted 68000s.** The FD1094 decrypts each opcode fetch according to a
  state the program can change, so the opcode image is built per word: each
  word takes the state MAME's runs fetched it in, or the state of the
  nearest executed word before it. The FD1089 decrypts with a fixed key, so
  its opcode and data images are made up front. Both take the key from
  MAME's set.

### Scheduling

Each board puts its CPUs and timers on one `sched::Scheduler`, MAME's
scheduler transplanted. Each timeslice ends at the next timer or quantum,
each CPU runs in order up to the slice's target, and a CPU that stops early
pulls the target back for the CPUs after it, as in MAME. The order of CPUs
and timers follows MAME's device order, because the shared RAM and the
latches are only right when that order is.

### Video

The video is MAME's drawing code, rendered per partial screen update,
because the games change scroll and road registers mid-frame:

- **Hang-On, Out Run, X Board, Y Board:** Sega's 16-bit tilemaps and text
  layer, the road generators (Hang-On, Space Harrier and Out Run variants),
  the board's sprite format, and the shadow and hilight mixing
  (`src/outrun/video`, configured per board through `VideoConfig`).
- **Y Board:** adds the rotation layer (`rotate_draw`) over the Y sprite
  bitmap, mixed with the System 16B sprites (`src/yboard/video`).
- **System 32:** tilemaps with zoom and rowscroll, text, the bitmap layer,
  sprites and the mixer (`src/system32/video`). Multi 32 has two of each;
  the core shows player one's screen.

The board renders into an indexed bitmap. `Video::rgb` applies MAME's
palette (resistor weights, shadow and hilight) when the frame is handed
over.

### Sound

- Hang-On board: YM2203 (ymfm OPN) and SegaPCM for Hang-On and Space
  Harrier; Enduro Racer has the YM2151 sound board instead.
- Out Run, X Board, Y Board: YM2151 (ymfm OPM) and SegaPCM, with the PCM
  bank layout per board. Super Monaco GP adds its second sound board.
- System 32: two YM3438s (ymfm OPN2) and an RF5C68; Multi 32 has a YM3438
  and MultiPCM.

The chips' timers, IRQs and busy flags go through MAME's glue, because the
sound CPUs poll them and the timing shows in the traces. The core hands the
app one stream per chip at its own rate, with MAME's speaker gains.

### The core library

`src/<board>/core.cpp` implements `afcore.h`. It creates the board from the
user's set, runs one frame per call, hands over the RGB frame and the audio
streams, maps the app's inputs onto the board's ports, and saves the
NVRAM. `check_roms` runs the importer without building anything, so the
app can say which files are wrong. Each set's core is selected at build
time (`C2D_ROMSET`) and exports one symbol, `afcore_api_<set>`.

Input mapping is per set, following MAME's input ports:

- Analogue controls go through the board's ADC with MAME's ranges and
  reversals: steering, pedals, flight sticks and throttles, gun aim.
- Digital controls: coins, start, test and service, the gear lever
  (toggled by the shift buttons), the buttons on the stick.

### Testing against MAME

Each board has a MAME trace patch (`patches/mame/0001` to `0006`) and a
native trace tool (`orun_trace`, `xbd_trace`, `ybd_trace`, `hang_trace`,
`s32_trace`; each a CMake target of the set's build). Both write the same
lines:

- **CPU snapshots:** registers every 4096 instructions per CPU, or every
  instruction in a chosen range.
- **Interrupts and timeslices:** to find where a divergence starts.
- **Screen updates:** a hash of the indexed and RGB bitmaps for each
  partial update, with the instruction counts at that moment. With
  `ORTRACE_HASH_CLIP=1` on both sides, the hash covers only the rows the
  update drew, because MAME alternates two bitmaps.

The input replays (`tests/replays/<set>_{test,game}.rpl`) set port values
at given screen updates, and MAME reads the same files. Runs that match
also feed the seeds: MAME's `ORTRACE_PCS` lists every address executed.

## Data and settings

- **NVRAM and EEPROM.** The board's own backup RAM or EEPROM is saved as
  `<set>.nv` in the save directory the app gives the core. It holds the
  operator settings and high scores. F1 Exhaust Note keeps both boards'
  EEPROMs in one file.
- **DIP switches.** Each set's defaults are MAME's, set in the core. The
  operator settings in each game's test mode cover the rest.
- **Versions.** Each core has its own x.y.z version in `games.toml`. The
  major number changes only when old saves stop carrying over.

## Platforms

The cores are plain C++20 with no platform code, so they build wherever the
app runs: macOS, iOS, iPadOS and tvOS (frameworks), Linux x86-64 and arm64.
The generated code is large (a 68000 program becomes several megabytes of
C++), so a build compiles at most two generated files at once.

Speed is not a concern on these boards. Over 3600 frames on the x86-64
Linux build host, the cores run at about 150 to 390 frames a second: Y
Board sets 155 to 390, X Board 230 to 330, System 32 about 210 to 236
(F1 Exhaust Note's two boards about 147), and the Hang-On board 245 to 338.

## Testing

- **Against MAME.** For every set: attract mode, test mode with every item
  walked, and a full game to game over, with every CPU snapshot, interrupt
  and screen update compared. The runs, their lengths and their results are
  in the HANDOFF entry for each game.
- **Regressions.** A change to shared code (the 68000 timing, the
  scheduler, the video) is checked on the other boards' sets: their traces
  and frame hashes must be identical before and after.
- **In CI.** Pull requests get the release scan and a ROM-free Linux build.
  The replays need ROMs, so they run in the app's nightly on a
  self-hosted runner, through the trace tools.
- **In the app.** `afcore_run SET FRAMES` runs a core headless through the
  library and prints a hash per frame. Device results go into the app's
  compatibility table.

## Open items

- A fallback interpreter for the V60, like the 68000's.
- Multi 32's second screen, and link play for the linked cabinets
  (OutRunners, F1 Exhaust Note's second seat).
- The 68000 decoder, the recompiler and the scheduler move to Common,
  shared with the Model 2 cores' sound board.
- The Turbo board ([TURBO.md](TURBO.md), #31).

## Decisions

- **MAME's scheduler, not a simpler loop.** Without MAME's exact
  interleaving the traces drift within seconds, and every later bug would
  hide behind that drift.
- **Whole-instruction recompiled code, with MAME's bus timing patched in.**
  MAME's cycle-exact 68000 can stop mid-instruction. Splitting and
  deferring the accesses that cross a slice end gives the same results
  without a microcode engine ([M68000.md](M68000.md)).
- **Musashi timing on the Y Board.** MAME runs the Y Board on Musashi, so
  the cycle-exact model would never match there.
- **MAME's own cores for the small CPUs.** The Z80, i8751 and V25 are
  generated from MAME's sources rather than recompiled: they are cheap to
  run, and using MAME's code keeps their timing identical.
- **No hand edits to generated code.** A problem is fixed in the recompiler
  or by adding a seed.
- **Seeds are addresses only.** They are our analysis of MAME runs and
  hold no ROM bytes, so they are committed. Trace logs and frame dumps are
  not.
