# Design documents

High-level designs for the Classic 2D cores: what each part is for, how it
is built and why, how it is tested and what is still open. They describe
the design as it is and are updated in the same pull request as a change
that moves it. The dated history is in [HANDOFF.md](../../HANDOFF.md) and
the current state in [STATE.md](../../STATE.md). Designs shared by every
core (the `afcore` interface, x86recomp) are in Arcade-Foundry-Cores-Common's
`docs/design/`; the app's are in the app repository's `docs/design/`.

| Document | Covers | State |
|---|---|---|
| [CLASSIC2D.md](CLASSIC2D.md) | The Classic 2D cores: boards, recompilers, scheduling, video, sound, the core library, testing against MAME | 26 sets on five boards match MAME; open items listed |
| [M68000.md](M68000.md) | The 68000 recompiler, MAME's cycle-exact timing, Musashi timing for the Y Board, the fallback interpreter | In use on every 68000 board |
| [TURBO.md](TURBO.md) | The Sega Z80-3D system: Turbo, Subroc-3D, Buck Rogers (#31) | Proposed, not started |

Each document follows the same outline: Purpose and scope, Requirements,
Design, Data and settings, Platforms, Testing, Open items, Decisions.
