// license:BSD-3-Clause
// copyright-holders:Aaron Giles
//
// The file lists, CRCs and layouts are MAME's ROM_START( orunners ) and
// ROM_START( radr ) (src/mame/sega/segas32.cpp).
#include "system32/rom_import.h"

#include <algorithm>

namespace s32 {
namespace {

using File = rt::RomFile;

// ROM_LOAD32_WORD_x4 (0x000000, 0x000002), ROM_LOAD32_WORD (0x100000, 0x100002)
const File kMainLo[2] = {{"epr-15620.ic37", 0x20000, 0x84f5ad92}, {"epr-15621.ic40", 0x20000, 0xd98b765a}};
const File kMainHi[2] = {{"mpr-15538.ic36", 0x80000, 0x93958820}, {"mpr-15539.ic39", 0x80000, 0x219760fa}};
const File kSound = {"epr-15550.ic31", 0x80000, 0x0205d2ed};
// ROM_LOAD16_BYTE
const File kTiles[2] = {{"mpr-15548.ic3", 0x200000, 0xb6470a66}, {"mpr-15549.ic11", 0x200000, 0x81d12520}};
// ROM_LOAD64_WORD at 0x000000 (+0, +2, +4, +6) and 0x800000
const File kSprites[8] = {{"mpr-15540.ic14", 0x200000, 0xa10d72b4}, {"mpr-15542.ic15", 0x200000, 0x40952374},
                          {"mpr-15544.ic10", 0x200000, 0x39e3df45}, {"mpr-15546.ic38", 0x200000, 0xe3fcc12c},
                          {"mpr-15541.ic22", 0x200000, 0xa2003c2d}, {"mpr-15543.ic23", 0x200000, 0x933e8e7b},
                          {"mpr-15545.ic18", 0x200000, 0x53dd0235}, {"mpr-15547.ic41", 0x200000, 0xedcb2a43}};
const File kPcm[2] = {{"mpr-15551.ic1", 0x200000, 0x4894bc73}, {"mpr-15552.ic2", 0x200000, 0x1c4b5e73}};

// radr (System 32): ROM_LOAD_x8, ROM_LOAD16_BYTE
const File kRadrMain = {"epr-14241.ic21", 0x20000, 0x59a5f63d};
const File kRadrMainHi[2] = {{"epr-14106.ic37", 0x80000, 0xe73c63bf}, {"epr-14107.ic38", 0x80000, 0x832f797a}};
// ROM_LOAD_x8 at 0, ROM_LOAD_x2 at 0x100000, 0x200000, 0x300000
const File kRadrSound = {"epr-14108.ic9", 0x20000, 0x38a99b4d};
const File kRadrSoundHi[3] = {{"epr-14109.ic14", 0x80000, 0xd5563697}, {"epr-14110.ic20", 0x80000, 0x33fd2913},
                              {"epr-14237.ic22", 0x80000, 0x0a4b4b29}};
// ROM_LOAD32_BYTE at +0, +2, +1, +3
const File kRadrTiles[4] = {{"epr-14102.ic3", 0x40000, 0x5626e80f}, {"epr-14104.ic12", 0x40000, 0xb0173646},
                            {"epr-14103.ic7", 0x40000, 0x08c7e804}, {"epr-14105.ic18", 0x40000, 0x614843b6}};
// ROM_LOAD64_BYTE at +0 .. +7
const File kRadrSprites[8] = {{"mpr-13511.ic1", 0x100000, 0xf8f15b11}, {"mpr-13512.ic5", 0x100000, 0xd0be34a6},
                              {"mpr-13513.ic10", 0x100000, 0xfeef1982}, {"mpr-13514.ic16", 0x100000, 0xd0f9ebd1},
                              {"mpr-13515.ic2", 0x100000, 0x77bf2387}, {"mpr-13516.ic6", 0x100000, 0x8c4bc62d},
                              {"mpr-13517.ic11", 0x100000, 0x1d7d84a7}, {"mpr-13518.ic17", 0x100000, 0x9ea4b15d}};
const File kRadrEeprom = {"eeprom-radr.ic76", 0x80, 0x602032c6};

Roms import_radr(const rt::RomSource &src) {
    Roms r;
    r.multi32 = false;
    r.comm_link = 14084;
    r.main.assign(0x200000, 0);
    {
        const auto d = src.get(kRadrMain);
        for (int copy = 0; copy < 8; ++copy) std::copy(d.begin(), d.end(), r.main.begin() + copy * 0x20000);
        const auto e = src.get(kRadrMainHi[0]), o = src.get(kRadrMainHi[1]);
        for (size_t i = 0; i < e.size(); ++i) {
            r.main[0x100000 + 2 * i] = e[i];
            r.main[0x100000 + 2 * i + 1] = o[i];
        }
    }
    r.sound.assign(0x400000, 0);
    {
        const auto d = src.get(kRadrSound);
        for (int copy = 0; copy < 8; ++copy) std::copy(d.begin(), d.end(), r.sound.begin() + copy * 0x20000);
        for (int i = 0; i < 3; ++i) {
            const auto h = src.get(kRadrSoundHi[i]);
            for (int copy = 0; copy < 2; ++copy) std::copy(h.begin(), h.end(), r.sound.begin() + 0x100000 * (i + 1) + copy * 0x80000);
        }
    }
    r.tiles.assign(0x100000, 0);
    {
        static const int lane[4] = {0, 1, 2, 3}; // kRadrTiles is in lane order
        for (int f = 0; f < 4; ++f) {
            const auto d = src.get(kRadrTiles[f]);
            for (size_t i = 0; i < d.size(); ++i) r.tiles[4 * i + size_t(lane[f])] = d[i];
        }
    }
    r.sprites.assign(0x800000, 0);
    for (int f = 0; f < 8; ++f) {
        const auto d = src.get(kRadrSprites[f]);
        for (size_t i = 0; i < d.size(); ++i) r.sprites[8 * i + size_t(f)] = d[i];
    }
    r.eeprom = src.get(kRadrEeprom);
    return r;
}

// ROM_LOAD32_WORD: the file's words at base, base + 4, ...
void load32_word(std::vector<uint8_t> &r, const std::vector<uint8_t> &d, size_t base) {
    for (size_t i = 0; i + 1 < d.size(); i += 2) {
        r[base + 2 * i] = d[i];
        r[base + 2 * i + 1] = d[i + 1];
    }
}

} // namespace

Roms import_rom_set(const std::string &path, const std::string &set) {
    const rt::RomSource src(path);
    if (set == kRadrRomSet) return import_radr(src);
    Roms r;
    r.main.assign(0x200000, 0);
    {
        const auto lo = src.get(kMainLo[0]), hi = src.get(kMainLo[1]);
        for (int copy = 0; copy < 4; ++copy) {
            load32_word(r.main, lo, size_t(copy) * 0x40000 + 0);
            load32_word(r.main, hi, size_t(copy) * 0x40000 + 2);
        }
        load32_word(r.main, src.get(kMainHi[0]), 0x100000);
        load32_word(r.main, src.get(kMainHi[1]), 0x100002);
    }
    r.sound = src.get(kSound);
    r.tiles = src.load16_byte(kTiles, 1, 0x400000);
    r.sprites.assign(0x1000000, 0);
    for (int g = 0; g < 2; ++g)
        for (int lane = 0; lane < 4; ++lane) {
            const auto d = src.get(kSprites[g * 4 + lane]);
            for (size_t i = 0; i + 1 < d.size(); i += 2) {
                r.sprites[size_t(g) * 0x800000 + 4 * i + size_t(lane) * 2] = d[i];
                r.sprites[size_t(g) * 0x800000 + 4 * i + size_t(lane) * 2 + 1] = d[i + 1];
            }
        }
    r.pcm.assign(0x400000, 0);
    for (int i = 0; i < 2; ++i) {
        const auto d = src.get(kPcm[i]);
        std::copy(d.begin(), d.end(), r.pcm.begin() + size_t(i) * 0x200000);
    }
    return r;
}

} // namespace s32
