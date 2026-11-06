# Arcade Foundry Cores: Classic 2D

Static recompilation cores for classic 2D arcade boards, for
[Arcade Foundry: Insert Coin](https://github.com/TheBrinkOfTomorrow/Arcade-Foundry-Insert-Coin).

Each game's original program code is translated ahead of time into native
C++ and runs on a native model of its board: no CPU emulator runs at play
time. MAME is the behavioural reference: every core is checked against it
frame by frame (attract, test mode and a full game to game over).

## Boards

| Board | Games (MAME sets) | Status |
| --- | --- | --- |
| Sega Hang-On board | hangon, sharrier, enduror | Matches MAME |
| Sega Out Run board | outrun (and outrunra, outruno, outrundx), shangon, toutrun | Matches MAME |
| Sega X Board | aburner2, aburner131, thndrbld, thndrbld1, rachero, smgp, loffire, gprider | Matches MAME |
| Sega Y Board | gforce2, pdrift, gloc, strkfgtr, rchase | Matches MAME |
| Sega System Multi 32 / System 32 | orunners, radr, radm, f1en, jpark, sonic, ga2 | Matches MAME |

How the cores are designed: [docs/design](docs/design/README.md).

## Building a core

You need CMake 3.20+, a C++20 compiler, Python 3 and git (CMake fetches
ymfm). From your MAME set:

    python3 scripts/recompile.py --set outrun --rom /path/to/outrun.zip

This checks every file's CRC, translates the two 68000 programs into C++
(`build/outrun/gen/`, derived from your ROMs: never commit it) and builds
`build/outrun/libafcore_outrun.so` (`afcore_outrun.framework` on macOS),
which exports one symbol, `afcore_api_outrun` (`common/include/afcore.h`).
`build/outrun/afcore_run ROMSET FRAMES` runs it headless.

Cross builds (iOS, tvOS, arm64 Linux) run `recompile.py --gen-only` on the
host, then configure CMake for the target with
`-DC2D_GEN_ROOT=build/outrun/gen -DC2D_TOOLS=OFF` and build `afcore_outrun`.

## ROMs

No ROM data is in this repository. You supply your own sets, named as in
MAME; the importer checks every file's CRC before anything is built.

## Licence

BSD-3-Clause (`LICENSE`). Third-party code and where it came from:
`THIRD_PARTY.md`.

## Version numbers

The app and each game core are versioned separately. The release number on this
repository (for example v1.0.0) is a snapshot label: it means "as shipped with
Arcade Foundry: Insert Coin 1.0.0". Every game core also has its own x.y.z
version (shown in the app), which can be higher or lower than the release number.
A core's major number only changes when its saves stop carrying over.
