# Release notes

Newest first. Each release is also on the repository's Releases page.

## 1.0.0 (2026-11-02)

**Arcade Foundry Cores: Classic 2D 1.0.0**

First release of the Classic 2D cores for Arcade Foundry: Insert Coin, as shipped with Insert Coin 1.0.0.

- **Games:** Out Run (`outrun`), Super Hang-On (`shangon`), After Burner II (`aburner2`) and Thunder Blade (`thndrbld`), from the Sega Out Run and X boards.
- **How it works:** each game's original program is translated ahead of time into native C++ and runs on a native model of its board, with no CPU emulator at play time. MAME is the reference every core is checked against, frame by frame.
- **Building:** `python3 scripts/recompile.py --set outrun --rom /path/to/outrun.zip`. Details in the README.

No ROM data is in this repository. The cores are built from your own ROM sets. BSD-3-Clause; third-party code is listed in `THIRD_PARTY.md`.

The release number is a snapshot label for "as shipped with Insert Coin 1.0.0"; each game core has its own version.
