#!/bin/sh
# Builds build/orun_trace (the native board with the trace hook) from
# work/outrun_{main,sub}.bin and the PC seed lists given as $1 $2.
# YMFM_DIR: ymfm's src/ (default: MAME's copy in extern/mame/3rdparty).
set -e
cd "$(dirname "$0")/.."
mkdir -p build
CXX=${CXX:-g++}
YMFM_DIR=${YMFM_DIR:-extern/mame/3rdparty/ymfm/src}
python3 src/z80/mame/z80make.py src/z80/mame/z80.lst build/z80.hxx
$CXX -O2 -std=c++20 -Isrc -o build/m68krecomp tools/m68krecomp/main.cpp src/m68k/decode.cpp src/m68k/reach.cpp
build/m68krecomp work/outrun_main.bin build/gen_main.cpp --ns orun_main --seeds "$1" --pointers --scan | head -1
build/m68krecomp work/outrun_sub.bin build/gen_sub.cpp --ns orun_sub --seeds "$2" --pointers --scan | head -1
HOOK='-DM68K_INSN_HOOK(pc)=do{if(c.insns>=e.hook_at)e.on_hook(pc);}while(0)'
$CXX -O1 -std=c++20 -Isrc "$HOOK" -c build/gen_main.cpp -o build/gen_main_t.o &
$CXX -O1 -std=c++20 -Isrc "$HOOK" -c build/gen_sub.cpp -o build/gen_sub_t.o &
for f in src/outrun/board.cpp src/outrun/screen.cpp src/outrun/sound.cpp src/outrun/video.cpp src/outrun/rom_import.cpp src/runtime/rom_source.cpp src/runtime/zip.cpp src/fd1094/fd1094.cpp src/outrun/m68k_exec.cpp src/sched/scheduler.cpp src/z80/z80.cpp tools/orun_trace/main.cpp; do
  $CXX -O2 -g -std=c++20 -Isrc -Ibuild -I$YMFM_DIR -Wall -c $f -o build/$(basename $f .cpp).o &
done
$CXX -O2 -std=c++20 -I$YMFM_DIR -w -c $YMFM_DIR/ymfm_opm.cpp -o build/ymfm_opm.o &
wait
$CXX -o build/orun_trace build/main.o build/board.o build/screen.o build/sound.o build/video.o build/rom_import.o build/rom_source.o build/zip.o build/fd1094.o build/m68k_exec.o build/scheduler.o build/z80.o build/ymfm_opm.o build/gen_main_t.o build/gen_sub_t.o
