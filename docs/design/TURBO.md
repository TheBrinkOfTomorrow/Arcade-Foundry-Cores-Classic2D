# Sega Z80-3D system: Turbo, Subroc-3D, Buck Rogers

A design for a board not built yet. Sega's early Z80 driving and shooting
hardware runs three games: Turbo (1981), Subroc-3D (1982) and Buck Rogers:
Planet of Zoom (1982). MAME calls it the "Sega Z80-3D system" (`turbo.cpp`,
`turbo_a.cpp`, `turbo_v.cpp`). Tracked in #31, targeted at app 1.4.0 or
1.5.0. Nothing here is implemented; this document says what the board needs
and where it differs from the other Classic 2D boards
([CLASSIC2D.md](CLASSIC2D.md)), so the work can be sized and started.

## Purpose and scope

Add the three games to the app on the same terms as the other Classic 2D
sets. The user supplies MAME's sets, and each game matches MAME in attract,
test mode and a full game.

| Set | Game | Year | Notes |
|---|---|---|---|
| `turbo` | Turbo (program 1513-1515) | 1981 | Not encrypted; `turboa` to `turboe` are encrypted revisions |
| `subroc3d` | Subroc-3D | 1982 | The periscope game; stereo view on the cabinet |
| `buckrog` | Buck Rogers: Planet of Zoom | 1982 | Encrypted; `buckrogn` is not |

## Requirements

The Classic 2D requirements apply unchanged: no ROM data in the repository,
MAME as the reference, one core per set behind `afcore.h`, the app's
controls, the user's set as it comes. Two need more care on this board:

1. **Sound.** MAME marks all three games' sound as imperfect. The real
   sound is analogue circuits, and MAME plays recorded samples triggered by
   the game's sound latches. The samples are not part of the ROM set, and
   recordings of the cabinet cannot go in the repository either. The core
   needs a sound source we can ship (see "Sound" below).
2. **The displays outside the screen.** Turbo's score, time and
   tachometer are LED displays driven by an Intel 8279, and lamps sit on an
   LS259 latch. MAME shows them through a layout file. The app has one
   screen per core, so the core has to draw them into the frame or hand
   them over another way.

## Design (proposed)

### What is the same as the other boards

- The board model is a transplant of MAME's driver (BSD-3-Clause): memory
  maps, the 8255 PPIs (`src/runtime/i8255`, already here), the 8279, the
  LS259, the coin and DIP switch inputs, the interrupt (IRQ0 at VBLANK).
- MAME's scheduler (`src/sched`), unchanged.
- The trace method: a MAME patch for the `turbo.cpp` driver and a native
  `turbo_trace` writing the same CPU snapshot, interrupt and frame hash
  lines, compared with the same scripts, plus input replays.
- `c2d_import` with MAME's ROM lists and CRCs; a `games.toml` entry per set;
  a new board id (`turbo`) in Common's `afgames.py` first.

### CPUs

The main CPU is a Z80 at 5 MHz (MASTER_CLOCK / 4). Buck Rogers adds a
second Z80 that draws the background bitmap from commands the main CPU
sends (`sub_prg_map`, `bitmap_w`, `subcpu_command_r`).

Two ways to run them:

- **MAME's Z80 core**, generated as for the sound CPUs (`src/z80`). It is
  cheap at 5 MHz and matches MAME by construction.
- **Recompiled**, with a Z80 recompiler from the Model 2 sound board
  copied in. That would be the first recompiled main Z80 here.

The proposal is MAME's core first: a 5 MHz Z80 costs little, and it gets
the board matching sooner. Recompiling can come later if any target needs
the speed.

The encrypted sets use Sega's 315-5xxx Z80 opcode encryption (MAME's
`SEGA_315_5014` for `buckrog` and `init_turbo_enc` for Turbo's encrypted
revisions): opcodes and data are decrypted differently, the same split the
FD1089 uses. With MAME's core, the decryption is a decrypted opcode map in
front of the CPU. The unencrypted sets (`turbo`, `buckrogn`) come first.

### Video

This is the largest part, because the board has no tilemap or sprite chip:
the picture is built by discrete logic and PROMs, and MAME draws it
scanline by scanline (`turbo_v.cpp`, about 1,000 lines, VIDEO_ALWAYS_UPDATE).

- Turbo: a character layer, the road (made from road ROMs and PROMs through
  the road drawing registers), and sprites expanded from sprite ROMs and
  scaled by PROM tables. A collision RAM is read back by the game.
- Subroc-3D: sprites and characters with the periscope's left and right
  views. MAME draws one image; the stereo view is open (see "Open items").
- Buck Rogers: the bitmap from the second Z80, sprites and characters, and
  a 1024-entry palette.

The transplant renders into an indexed bitmap per scanline, and the palette
uses MAME's resistor weights, as the other boards do. The collision results
must match MAME's exactly, because the game reads them.

### Sound

Each game writes sound triggers through the 8255 ports. MAME maps them to
sample start, stop and pitch (Turbo's engine pitch follows the accelerator;
the ambulance, crash, screech and skid sounds are one-shots). Options for
a sound source we can ship:

1. **Synthesise each sound** from the schematics' circuits (engine, noise,
   tones), driven by the same triggers MAME uses. This is fully ours to
   distribute. It is the most work, and the result would differ from MAME,
   which only has samples.
2. **Load the user's own sample set** (MAME's `turbo`, `subroc3d` and
   `buckrog` sample zips) beside the ROM set, as MAME does, and play them
   with MAME's routing and gains. No sound if they are missing.
3. Both: samples when present, synthesis otherwise.

The proposal is option 2 first, because it matches MAME and is small, then
option 1 as a later improvement. Either way the traces compare the
triggers, not the audio.

### Inputs and the app

- Turbo: the wheel (a dial in MAME, so relative steering), the accelerator
  pedal and the gear shift: the app's drive controls, with the gear on the
  shift buttons.
- Subroc-3D: the periscope moves in four directions with one fire button,
  so the left stick and a fire button.
- Buck Rogers: a stick, three buttons and an accelerator, which is a pedal
  or a button depending on the "Accel by" DIP switch (MAME's default is
  the button).

The LED displays and lamps: the proposal is that the core draws Turbo's
score, time and tachometer into a strip around the picture (the frame size
stays fixed per set), so every platform shows them with no app change.

## Data and settings

- The DIP switches hold the operator settings (game time, extended play,
  difficulty, coinage, the tachometer type, the sound system, and the
  others' motion and screen settings). The board
  has no NVRAM, so nothing is saved. A core option for the DIP switches may
  be needed, since there is no test menu to change them in.
- The user's sample sets, if option 2 is taken, are found beside the ROM
  set.

## Platforms

The same as the other Classic 2D cores. The board is light (5 MHz Z80s,
one 256x224-class screen), so speed is not a concern on any platform.

## Testing

As for every Classic 2D board: attract, test mode where the game has one,
and a full game, compared with MAME's traces CPU by CPU and frame by frame,
with input replays committed. The sound triggers are compared in the
traces. Device runs go into the app's compatibility table.

## Open items

- ROMs: none on hand yet.
- Sound: choose between the user's samples and synthesis.
- How to show Turbo's LED displays and Subroc-3D's stereo view (the app's
  stereo and VR work could show it as intended).
- Whether the DIP switches need a core option in the app.

## Decisions

None taken yet; the proposals above are for review when the work starts.
