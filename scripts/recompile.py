#!/usr/bin/env python3
"""Recompile a game from the user's ROM set and build its core.

  recompile.py --set SET --rom ROMSET [--build-dir DIR] [--core | --gen-only] [--jobs N]

SET is one of games.toml's sets. Builds the host tools (CMake, in DIR), checks the
set and writes the 68000 program images (c2d_import), translates them (m68krecomp, seeded with
seeds/<set>.main and seeds/<set>.sub (Y Board: .main, .subx, .suby), plus the jump tables and handler
pointers it finds in the program) into DIR/gen/<set>/, then with --core
builds the core library afcore_<set> (a framework on macOS). Everything it
writes is derived from the ROMs: keep DIR under build/ (git-ignored).
Cross builds (iOS, tvOS, arm64 Linux) configure CMake with
-DC2D_GEN_ROOT=DIR/gen and build the afcore_<set> target.
"""
import argparse
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
from game_sets import SETS  # set -> generated code namespace prefix (games.toml)


def run(cmd):
    print("+", " ".join(cmd), flush=True)
    subprocess.run(cmd, check=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--set", required=True, choices=sorted(SETS))
    ap.add_argument("--rom", required=True, help="the MAME set (.zip) or a folder of its files")
    ap.add_argument("--build-dir", default=None)
    mode = ap.add_mutually_exclusive_group()
    mode.add_argument("--core", action="store_true", help="also build the core library (the default)")
    mode.add_argument("--gen-only", action="store_true", help="only generate the code")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    args = ap.parse_args()

    s = args.set
    build = os.path.abspath(args.build_dir or os.path.join(ROOT, "build", s))
    gen = os.path.join(build, "gen", s)
    images = os.path.join(build, "images")
    os.makedirs(gen, exist_ok=True)
    os.makedirs(images, exist_ok=True)
    configure = ["cmake", "-S", ROOT, "-B", build, "-DCMAKE_BUILD_TYPE=Release", f"-DC2D_ROMSET={s}",
                 f"-DC2D_GEN_ROOT={os.path.join(build, 'gen')}"]
    run(configure)
    run(["cmake", "--build", build, "--parallel", str(args.jobs), "--target", "m68krecomp", "v60recomp", "c2d_import_tool"])
    exe = lambda name: os.path.join(build, name + (".exe" if os.name == "nt" else ""))
    if SETS[s] == "s32":
        run([exe("c2d_import"), s, os.path.abspath(args.rom), images])
        cmd = [exe("v60recomp"), os.path.join(images, f"{s}_main.bin"), gen, "--code", "000000-200000",
               "--code", "f00000-1000000", "--name", s]
        seeds = os.path.join(ROOT, "seeds", f"{s}.v60")
        if os.path.exists(seeds):
            cmd += ["--seeds", seeds]
        run(cmd)
        if not args.gen_only:
            run(configure)
            run(["cmake", "--build", build, "--parallel", str(args.jobs), "--target", f"afcore_{s}"])
        return
    # an FD1094 set's states come with its seeds (seeds/<set>.main)
    run([exe("c2d_import"), s, os.path.abspath(args.rom), images, os.path.join(ROOT, "seeds", f"{s}.main")])
    # the Y Board's three 68000s run as MAME's Musashi core runs them
    cpus = ("main", "subx", "suby") if SETS[s] == "ybd" else ("main", "sub")
    for cpu in cpus:
        cmd = [exe("m68krecomp"), os.path.join(images, f"{s}_{cpu}.bin"), os.path.join(gen, f"gen_{cpu}.cpp"),
               "--ns", f"{SETS[s]}_{cpu}", "--seeds", os.path.join(ROOT, "seeds", f"{s}.{cpu}"), "--pointers", "--scan"]
        if SETS[s] == "ybd":
            cmd.append("--musashi")
        op = os.path.join(images, f"{s}_{cpu}_op.bin")
        if os.path.exists(op):
            cmd += ["--opcodes", op]
        run(cmd)
    if args.gen_only:
        return
    run(configure)  # picks up the generated code
    run(["cmake", "--build", build, "--parallel", str(args.jobs), "--target", f"afcore_{s}"])


if __name__ == "__main__":
    main()
