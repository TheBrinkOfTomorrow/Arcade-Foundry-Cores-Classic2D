#!/usr/bin/env python3
"""Write the files that list this repo's games from games.toml.

  gen_games.py          rewrite them
  gen_games.py --check  only compare (CI): exit 1 when one is out of date

Writes cmake/games.cmake (C2D_SETS, each set's board and core version) and
scripts/game_sets.py (recompile.py's sets and their boards). A new game: a
[[game]] in games.toml, then this.
"""

import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "common", "tools", "games"))
import afgames  # noqa: E402

HEADER = "# Written by scripts/gen_games.py from games.toml: edit that, not this.\n"
# board -> the generated code's namespace prefix (recompile.py)
PREFIX = {"outrun": "orun", "xboard": "xbd", "system32": "s32", "hangon": "hang", "yboard": "ybd"}


def generate(out):
    games = afgames.load_core(os.path.join(ROOT, "games.toml"))
    for g in games:
        if g["board"] not in PREFIX:
            raise afgames.ManifestError(f"{g['set']}: board {g['board']} is not one of this repo's ({', '.join(PREFIX)})")
    cm = [HEADER, "# The sets (C2D_ROMSET), each one's board (src/<board>/) and core version\n",
          f"set(C2D_SETS {afgames.cmake_list(g['set'] for g in games)})\n"]
    for g in games:
        cm.append(f"set(C2D_BOARD_{g['set']} {g['board']})\n")
        cm.append(f"set(C2D_CORE_VERSION_{g['set']} {','.join(map(str, afgames.version_tuple(g)))})\n")
    out.write("cmake/games.cmake", "".join(cm))
    sets = ", ".join(f"{g['set']!r}: {PREFIX[g['board']]!r}" for g in games)
    out.write("scripts/game_sets.py",
              '"""The sets, written by scripts/gen_games.py from games.toml: edit that, not this."""\n\n'
              f"# set -> generated code namespace prefix (its board)\nSETS = {{{sets}}}\n")


if __name__ == "__main__":
    afgames.main(generate, ROOT, __doc__)
