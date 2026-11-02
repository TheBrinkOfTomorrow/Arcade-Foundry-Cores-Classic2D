// Checks the user's ROM set against MAME's ROM_START and writes the two
// 68000 program images the recompiler reads:
//   c2d_import SET ROMSET OUTDIR [STATES]  ->  OUTDIR/<SET>_main.bin, <SET>_sub.bin
// For an FD1094 set (toutrun) it also writes <SET>_main_op.bin, the opcodes
// decrypted in the states listed in STATES (seeds/<set>.main: "PC STATE").
// For orunners and radr (a V70, a V60) it writes OUTDIR/SET_main.bin, the CPU's 24-bit
// space as v60recomp reads it: the ROM at 0, its first MiB again at f00000.
// SET is a MAME set name (outrun, shangon, toutrun, aburner2, thndrbld, thndrbld1, orunners, radr); ROMSET a .zip or a
// folder of its files.
#include "outrun/rom_import.h"
#include "xboard/rom_import.h"
#include "system32/rom_import.h"
#include "fd1094/opcode_image.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <fstream>
#include <string>

int main(int argc, char **argv) {
    if (argc != 4 && argc != 5) {
        std::fprintf(stderr, "usage: c2d_import SET ROMSET OUTDIR [STATES]\n");
        return 2;
    }
    try {
        std::vector<uint8_t> main_img, sub_img, op_img;
        const std::string set = argv[1];
        if (set == orun::kRomSet || set == orun::kShangonRomSet || set == orun::kToutrunRomSet) {
            const orun::Game g = set == orun::kRomSet ? orun::Game::outrun : set == orun::kShangonRomSet ? orun::Game::shangon : orun::Game::toutrun;
            orun::Roms r = orun::import_rom_set(argv[2], g);
            if (!r.fd1094_key.empty()) {
                const auto pcs = argc == 5 ? fd1094::read_state_seeds(argv[4]) : decltype(fd1094::read_state_seeds("")){};
                if (pcs.empty()) throw std::runtime_error("an FD1094 set needs the states of MAME's run (STATES: \"PC STATE\" lines)");
                fd1094::ImageReport rep;
                op_img = fd1094::opcode_image(r.main, fd1094::Fd1094(r.fd1094_key), pcs, &rep);
                std::printf("c2d_import: FD1094 opcodes from %zu PCs, default state %02x,", rep.listed, rep.default_state);
                for (const auto &[s, n] : rep.states) std::printf(" %02x:%zu", s, n);
                std::printf(", %zu conflicting words\n", rep.conflicts);
            }
            main_img = std::move(r.main);
            sub_img = std::move(r.sub);
        } else if (set == xbd::kRomSet || set == xbd::kThndrbldRomSet || set == xbd::kThndrbld1RomSet) {
            const xbd::Game g = set == xbd::kRomSet ? xbd::Game::aburner2 : set == xbd::kThndrbldRomSet ? xbd::Game::thndrbld : xbd::Game::thndrbld1;
            xbd::Roms r = xbd::import_rom_set(argv[2], g);
            if (!r.fd1094_key.empty()) {
                const auto pcs = argc == 5 ? fd1094::read_state_seeds(argv[4]) : decltype(fd1094::read_state_seeds("")){};
                if (pcs.empty()) throw std::runtime_error("an FD1094 set needs the states of MAME's run (STATES: \"PC STATE\" lines)");
                fd1094::ImageReport rep;
                op_img = fd1094::opcode_image(r.main, fd1094::Fd1094(r.fd1094_key), pcs, &rep);
                std::printf("c2d_import: FD1094 opcodes from %zu PCs, default state %02x,", rep.listed, rep.default_state);
                for (const auto &[s, n] : rep.states) std::printf(" %02x:%zu", s, n);
                std::printf(", %zu conflicting words\n", rep.conflicts);
            }
            main_img = std::move(r.main);
            sub_img = std::move(r.sub);
        } else if (set == s32::kOrunnersRomSet || set == s32::kRadrRomSet) {
            s32::Roms r = s32::import_rom_set(argv[2], set);
            main_img.assign(0x1000000, 0xff);
            std::copy(r.main.begin(), r.main.end(), main_img.begin());
            std::copy(r.main.begin(), r.main.begin() + 0x100000, main_img.begin() + 0xf00000);
        } else {
            std::fprintf(stderr, "c2d_import: unknown set %s\n", argv[1]);
            return 2;
        }
        const std::string out = argv[3];
        for (const auto &[name, img] : {std::pair{"main", &main_img}, std::pair{"sub", &sub_img}, std::pair{"main_op", &op_img}}) {
            if (img->empty()) continue;
            std::ofstream f(out + "/" + set + "_" + name + ".bin", std::ios::binary);
            f.write(reinterpret_cast<const char *>(img->data()), std::streamsize(img->size()));
            if (!f) throw std::runtime_error("cannot write " + out);
        }
    } catch (const std::exception &e) {
        std::fprintf(stderr, "c2d_import: %s\n", e.what());
        return 1;
    }
    return 0;
}
