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

// radm (System 32): radr's layout, 0x200000 of tiles; no comm link type
// (init_radm leaves the s32comm idle)
const File kRadmMain = {"epr-13693.ic21", 0x20000, 0x3f09a211};
const File kRadmMainHi[2] = {{"epr-13525.ic37", 0x80000, 0x62ad83a0}, {"epr-13526.ic38", 0x80000, 0x59ea372a}};
const File kRadmSound = {"epr-13527.ic9", 0x20000, 0xa2e3fbbe};
const File kRadmSoundHi[3] = {{"epr-13523.ic14", 0x80000, 0xd5563697}, {"epr-13699.ic20", 0x80000, 0x33fd2913},
                              {"epr-13523.ic22", 0x80000, 0xd5563697}};
const File kRadmTiles[4] = {{"mpr-13519.ic3", 0x80000, 0xbedc9534}, {"mpr-13521.ic12", 0x80000, 0xe9bca903},
                            {"mpr-13520.ic7", 0x80000, 0x3532e91a}, {"mpr-13522.ic18", 0x80000, 0x25e04648}};
const File kRadmEeprom = {"eeprom-radm.ic76", 0x80, 0xb1737c06};

// f1en (System 32, two boards with the same ROMs): ROM_LOAD_x8, then
// ROM_LOAD16_BYTE_x2 at 0x100000; sound as radr's; ROM_LOAD32_BYTE at +0,
// +2, +1, +3; ROM_LOAD64_BYTE of 0x80000 files into a 0x800000 region
const File kF1enMain = {"epr-14452a.ic6", 0x20000, 0xb5b4a9d9};
const File kF1enMainHi[2] = {{"epr-14445.ic14", 0x40000, 0xd06261ab}, {"epr-14444.ic7", 0x40000, 0x07724354}};
const File kF1enSound = {"epr-14449.ic35", 0x20000, 0x2d29699c};
const File kF1enSoundHi[3] = {{"epr-14448.ic31", 0x80000, 0x87ca1e8d}, {"epr-14447.ic26", 0x80000, 0xdb1cfcbd},
                              {"epr-14446.ic22", 0x80000, 0x646ec2cb}};
const File kF1enTiles[4] = {{"mpr-14362.ic38", 0x40000, 0xfb1c4e79}, {"mpr-14360.ic29", 0x40000, 0xc5e8da79},
                            {"mpr-14361.ic34", 0x40000, 0xe3204bda}, {"mpr-14359.ic25", 0x40000, 0x70305c68}};
const File kF1enSprites[8] = {{"mpr-14370.ic36", 0x80000, 0xfda78289}, {"mpr-14369.ic32", 0x80000, 0x7765116d},
                              {"mpr-14368.ic27", 0x80000, 0x5744a30e}, {"mpr-14367.ic23", 0x80000, 0x77bb9003},
                              {"mpr-14366.ic37", 0x80000, 0x21078e83}, {"mpr-14365.ic33", 0x80000, 0x36913790},
                              {"mpr-14364.ic28", 0x80000, 0x0fa12ecd}, {"mpr-14363.ic24", 0x80000, 0xf3427a56}};

Roms import_f1en(const rt::RomSource &src) {
    Roms r;
    r.multi32 = false;
    r.comm_link = 0;
    r.main.assign(0x200000, 0);
    {
        const auto d = src.get(kF1enMain);
        for (int copy = 0; copy < 8; ++copy) std::copy(d.begin(), d.end(), r.main.begin() + copy * 0x20000);
        const auto e = src.get(kF1enMainHi[0]), o = src.get(kF1enMainHi[1]);
        for (int copy = 0; copy < 2; ++copy)
            for (size_t i = 0; i < e.size(); ++i) {
                r.main[0x100000 + copy * 0x80000 + 2 * i] = e[i];
                r.main[0x100000 + copy * 0x80000 + 2 * i + 1] = o[i];
            }
    }
    r.sound.assign(0x400000, 0);
    {
        const auto d = src.get(kF1enSound);
        for (int copy = 0; copy < 8; ++copy) std::copy(d.begin(), d.end(), r.sound.begin() + copy * 0x20000);
        for (int i = 0; i < 3; ++i) {
            const auto h = src.get(kF1enSoundHi[i]);
            for (int copy = 0; copy < 2; ++copy) std::copy(h.begin(), h.end(), r.sound.begin() + 0x100000 * (i + 1) + copy * 0x80000);
        }
    }
    r.tiles.assign(0x100000, 0);
    for (int f = 0; f < 4; ++f) { // kF1enTiles is in lane order
        const auto d = src.get(kF1enTiles[f]);
        for (size_t i = 0; i < d.size(); ++i) r.tiles[4 * i + size_t(f)] = d[i];
    }
    r.sprites.assign(0x800000, 0);
    for (int f = 0; f < 8; ++f) {
        const auto d = src.get(kF1enSprites[f]);
        for (size_t i = 0; i < d.size(); ++i) r.sprites[8 * i + size_t(f)] = d[i];
    }
    return r;
}

// jpark (System 32, World Rev A): ROM_LOAD_x2 main, ROM_LOAD16_BYTE at
// 0x100000; sound ROM_LOAD_x4 then three 1 MiB files; ROM_LOAD16_BYTE tiles;
// ROM_LOAD64_WORD sprites as OutRunners' (0x000000 +0/+2/+4/+6, 0x800000).
// init_jpark patches two words at 0xc15a8 (the drive board MAME lacks).
const File kJparkMain = {"epr-16402a.ic8", 0x80000, 0xc70db239};
const File kJparkMainHi[2] = {{"epr-16395.ic18", 0x80000, 0xac5a01d6}, {"epr-16394.ic9", 0x80000, 0xc08c3a8a}};
const File kJparkSound = {"epr-16399.ic36", 0x40000, 0xb09b2fe3};
const File kJparkSoundHi[3] = {{"mpr-16398.ic35", 0x100000, 0xfa710ca6}, {"mpr-16397.ic34", 0x100000, 0x6e96e0be},
                               {"mpr-16396.ic24", 0x100000, 0xf69a2dc4}};
const File kJparkTiles[2] = {{"mpr-16404.ic14", 0x200000, 0x11283807}, {"mpr-16403.ic5", 0x200000, 0x02530a9b}};
const File kJparkSprites[8] = {{"mpr-16405.ic32", 0x200000, 0xb425f182}, {"mpr-16407.ic30", 0x200000, 0xbc49ffd9},
                               {"mpr-16409.ic28", 0x200000, 0xfe73660d}, {"mpr-16411.ic26", 0x200000, 0x71cabbc5},
                               {"mpr-16406.ic31", 0x200000, 0xb9ed73d6}, {"mpr-16408.ic29", 0x200000, 0x7b2f476b},
                               {"mpr-16410.ic27", 0x200000, 0x49c8f952}, {"mpr-16412.ic25", 0x200000, 0x105dc26e}};

Roms import_jpark(const rt::RomSource &src) {
    Roms r;
    r.multi32 = false;
    r.comm_link = 0;
    r.analog_rest = {0x80, 0x80, 0x80, 0x80, 0x00, 0x00, 0x00, 0x00};
    r.main.assign(0x200000, 0);
    {
        const auto d = src.get(kJparkMain);
        for (int copy = 0; copy < 2; ++copy) std::copy(d.begin(), d.end(), r.main.begin() + copy * 0x80000);
        const auto e = src.get(kJparkMainHi[0]), o = src.get(kJparkMainHi[1]);
        for (size_t i = 0; i < e.size(); ++i) {
            r.main[0x100000 + 2 * i] = e[i];
            r.main[0x100000 + 2 * i + 1] = o[i];
        }
        // pROM[0xc15a8/2] = 0xcd70; pROM[0xc15aa/2] = 0xd8cd (little-endian words)
        r.main[0xc15a8] = 0x70;
        r.main[0xc15a9] = 0xcd;
        r.main[0xc15aa] = 0xcd;
        r.main[0xc15ab] = 0xd8;
    }
    r.sound.assign(0x400000, 0);
    {
        const auto d = src.get(kJparkSound);
        for (int copy = 0; copy < 4; ++copy) std::copy(d.begin(), d.end(), r.sound.begin() + copy * 0x40000);
        for (int i = 0; i < 3; ++i) {
            const auto h = src.get(kJparkSoundHi[i]);
            std::copy(h.begin(), h.end(), r.sound.begin() + 0x100000 * (i + 1));
        }
    }
    r.tiles.assign(0x400000, 0);
    for (int f = 0; f < 2; ++f) {
        const auto d = src.get(kJparkTiles[f]);
        for (size_t i = 0; i < d.size(); ++i) r.tiles[2 * i + size_t(f)] = d[i];
    }
    r.sprites.assign(0x1000000, 0);
    for (int g = 0; g < 2; ++g)
        for (int lane = 0; lane < 4; ++lane) {
            const auto d = src.get(kJparkSprites[g * 4 + lane]);
            for (size_t i = 0; i + 1 < d.size(); i += 2) {
                r.sprites[size_t(g) * 0x800000 + 4 * i + size_t(lane) * 2] = d[i];
                r.sprites[size_t(g) * 0x800000 + 4 * i + size_t(lane) * 2 + 1] = d[i + 1];
            }
        }
    return r;
}

// sonic (System 32 trackball board, Japan rev. C): ROM_LOAD_x4 at 0 and
// 0x80000, ROM_LOAD16_BYTE_x2 at 0x100000; sound as jpark's; ROM_LOAD16_BYTE
// tiles (0x200000); ROM_LOAD64_WORD sprites, the second four 1 MiB files at
// 0x800000 (the rest of the 0x1000000 region stays 0)
const File kSonicMain[2] = {{"epr-15787c.ic17", 0x20000, 0x25e3c27e}, {"epr-15786c.ic8", 0x20000, 0xefe9524c}};
const File kSonicMainHi[2] = {{"epr-15781c.ic18", 0x40000, 0x65b06c25}, {"epr-15780c.ic9", 0x40000, 0x2db66fd2}};
const File kSonicSound = {"epr-15785.ic36", 0x40000, 0x0fe7422e};
const File kSonicSoundHi[3] = {{"mpr-15784.ic35", 0x100000, 0x42f06714}, {"mpr-15783.ic34", 0x100000, 0xe4220eea},
                               {"mpr-15782.ic33", 0x100000, 0xcf56b5a0}};
const File kSonicTiles[2] = {{"mpr-15789.ic14", 0x100000, 0x4378f12b}, {"mpr-15788.ic5", 0x100000, 0xa6ed5d7a}};
const File kSonicSprites[8] = {{"mpr-15790.ic32", 0x200000, 0xc69d51b1}, {"mpr-15792.ic30", 0x200000, 0x1006bb67},
                               {"mpr-15794.ic28", 0x200000, 0x8672b480}, {"mpr-15796.ic26", 0x200000, 0x95b8011c},
                               {"mpr-15791.ic31", 0x100000, 0x42217066}, {"mpr-15793.ic29", 0x100000, 0x75bafe55},
                               {"mpr-15795.ic27", 0x100000, 0x7f3dad30}, {"mpr-15797.ic25", 0x100000, 0x013c6cab}};

Roms import_sonic(const rt::RomSource &src) {
    Roms r;
    r.multi32 = false;
    r.comm_link = 0;
    r.trackball = true;
    r.sonic_protection = true;
    r.main.assign(0x200000, 0);
    for (int h = 0; h < 2; ++h) {
        const auto d = src.get(kSonicMain[h]);
        for (int copy = 0; copy < 4; ++copy) std::copy(d.begin(), d.end(), r.main.begin() + h * 0x80000 + copy * 0x20000);
    }
    {
        const auto e = src.get(kSonicMainHi[0]), o = src.get(kSonicMainHi[1]);
        for (int copy = 0; copy < 2; ++copy)
            for (size_t i = 0; i < e.size(); ++i) {
                r.main[0x100000 + copy * 0x80000 + 2 * i] = e[i];
                r.main[0x100000 + copy * 0x80000 + 2 * i + 1] = o[i];
            }
    }
    r.sound.assign(0x400000, 0);
    {
        const auto d = src.get(kSonicSound);
        for (int copy = 0; copy < 4; ++copy) std::copy(d.begin(), d.end(), r.sound.begin() + copy * 0x40000);
        for (int i = 0; i < 3; ++i) {
            const auto h = src.get(kSonicSoundHi[i]);
            std::copy(h.begin(), h.end(), r.sound.begin() + 0x100000 * (i + 1));
        }
    }
    r.tiles.assign(0x200000, 0);
    for (int f = 0; f < 2; ++f) {
        const auto d = src.get(kSonicTiles[f]);
        for (size_t i = 0; i < d.size(); ++i) r.tiles[2 * i + size_t(f)] = d[i];
    }
    r.sprites.assign(0x1000000, 0);
    for (int g = 0; g < 2; ++g)
        for (int lane = 0; lane < 4; ++lane) {
            const auto d = src.get(kSonicSprites[g * 4 + lane]);
            for (size_t i = 0; i + 1 < d.size(); i += 2) {
                r.sprites[size_t(g) * 0x800000 + 4 * i + size_t(lane) * 2] = d[i];
                r.sprites[size_t(g) * 0x800000 + 4 * i + size_t(lane) * 2 + 1] = d[i + 1];
            }
        }
    return r;
}

// Golden Axe: The Revenge of Death Adder (World, Rev B)
const File kGa2Main[2] = {{"epr-14961b.ic17", 0x20000, 0xd9cd8885}, {"epr-14958b.ic8", 0x20000, 0x0be324a3}};
const File kGa2MainHi[2] = {{"epr-15148b.ic18", 0x40000, 0xc477a9fd}, {"epr-15147b.ic9", 0x40000, 0x1bb676ea}};
const File kGa2Sound = {"epr-14945.ic36", 0x10000, 0x4781d4cb};
const File kGa2SoundHi[3] = {{"mpr-14944.ic35", 0x100000, 0xfd4d4b86}, {"mpr-14943.ic34", 0x100000, 0x24d40333},
                             {"mpr-14942.ic24", 0x100000, 0xa89b0e90}};
const File kGa2Mcu = {"epr-14468-02.u3", 0x10000, 0x77634daa};
const File kGa2Tiles[2] = {{"mpr-14948.ic14", 0x200000, 0x75050d4a}, {"mpr-14947.ic5", 0x200000, 0xb53e62f4}};
const File kGa2Sprites[8] = {{"mpr-14949.ic32", 0x200000, 0x152c716c}, {"mpr-14951.ic30", 0x200000, 0xfdb1a534},
                             {"mpr-14953.ic28", 0x200000, 0x33bd1c15}, {"mpr-14955.ic26", 0x200000, 0xe42684aa},
                             {"mpr-14950.ic31", 0x200000, 0x15fd0026}, {"mpr-14952.ic29", 0x200000, 0x96f96613},
                             {"mpr-14954.ic27", 0x200000, 0x39b2ac9e}, {"mpr-14956.ic25", 0x200000, 0x298fca50}};

// segas32_v25_state::ga2_opcode_table (segas32_m.cpp): the V25's opcode
// decryption (0x00 where MAME's table has no entry)
const uint8_t kGa2Opcodes[256] = {
		0x00,0x00,0xEA,0x00,0x00,0x8B,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFA,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x3B,0x00,0x49,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0xE8,0x00,0x00,0x75,0x00,0x00,0x00,0x00,0x3A,0x00,0x00,
		0x00,0x00,0x00,0x00,0x8D,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xBF,0x00,0x88,0x00,
		0x00,0x00,0x00,0x81,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xBC,
		0x00,0x00,0x00,0x8A,0x00,0x00,0x00,0x00,0x00,0x00,0x83,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xB8,0x26,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xB5,0x00,0xEB,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xB2,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0xC3,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xB9,0xBB,0x00,0x43,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x8E,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xBE,0x00,0x80,0x00,0x00
};

Roms import_ga2(const rt::RomSource &src) {
    Roms r;
    r.multi32 = false;
    r.comm_link = 0;
    r.four_player = true;
    r.main.assign(0x200000, 0);
    for (int h = 0; h < 2; ++h) {
        const auto d = src.get(kGa2Main[h]);
        for (int copy = 0; copy < 4; ++copy) std::copy(d.begin(), d.end(), r.main.begin() + h * 0x80000 + copy * 0x20000);
    }
    {
        const auto e = src.get(kGa2MainHi[0]), o = src.get(kGa2MainHi[1]);
        for (int copy = 0; copy < 2; ++copy)
            for (size_t i = 0; i < e.size(); ++i) {
                r.main[0x100000 + copy * 0x80000 + 2 * i] = e[i];
                r.main[0x100000 + copy * 0x80000 + 2 * i + 1] = o[i];
            }
    }
    r.sound.assign(0x400000, 0);
    {
        const auto d = src.get(kGa2Sound);
        for (int copy = 0; copy < 16; ++copy) std::copy(d.begin(), d.end(), r.sound.begin() + copy * 0x10000);
        for (int i = 0; i < 3; ++i) {
            const auto h = src.get(kGa2SoundHi[i]);
            std::copy(h.begin(), h.end(), r.sound.begin() + 0x100000 * (i + 1));
        }
    }
    {
        // decrypt_protrom: unscramble the address lines
        const auto d = src.get(kGa2Mcu);
        static const int kBits[16] = {14, 11, 15, 12, 13, 4, 3, 7, 5, 10, 2, 8, 9, 6, 1, 0}; // bitswap<16>, bit 15 first
        r.mcu.assign(0x10000, 0);
        for (unsigned i = 0; i < 0x10000; ++i) {
            unsigned src_addr = 0;
            for (int b = 0; b < 16; ++b) src_addr |= ((i >> kBits[b]) & 1u) << (15 - b);
            r.mcu[i] = d[src_addr];
        }
        r.mcu_opcodes = kGa2Opcodes;
    }
    r.tiles.assign(0x400000, 0);
    for (int f = 0; f < 2; ++f) {
        const auto d = src.get(kGa2Tiles[f]);
        for (size_t i = 0; i < d.size(); ++i) r.tiles[2 * i + size_t(f)] = d[i];
    }
    r.sprites.assign(0x1000000, 0);
    for (int g = 0; g < 2; ++g)
        for (int lane = 0; lane < 4; ++lane) {
            const auto d = src.get(kGa2Sprites[g * 4 + lane]);
            for (size_t i = 0; i + 1 < d.size(); i += 2) {
                r.sprites[size_t(g) * 0x800000 + 4 * i + size_t(lane) * 2] = d[i];
                r.sprites[size_t(g) * 0x800000 + 4 * i + size_t(lane) * 2 + 1] = d[i + 1];
            }
        }
    return r;
}

Roms import_radr(const rt::RomSource &src, bool radm) {
    Roms r;
    r.multi32 = false;
    r.comm_link = radm ? 0 : 14084;
    r.main.assign(0x200000, 0);
    {
        const auto d = src.get(radm ? kRadmMain : kRadrMain);
        for (int copy = 0; copy < 8; ++copy) std::copy(d.begin(), d.end(), r.main.begin() + copy * 0x20000);
        const File *hi = radm ? kRadmMainHi : kRadrMainHi;
        const auto e = src.get(hi[0]), o = src.get(hi[1]);
        for (size_t i = 0; i < e.size(); ++i) {
            r.main[0x100000 + 2 * i] = e[i];
            r.main[0x100000 + 2 * i + 1] = o[i];
        }
    }
    r.sound.assign(0x400000, 0);
    {
        const auto d = src.get(radm ? kRadmSound : kRadrSound);
        for (int copy = 0; copy < 8; ++copy) std::copy(d.begin(), d.end(), r.sound.begin() + copy * 0x20000);
        for (int i = 0; i < 3; ++i) {
            const auto h = src.get((radm ? kRadmSoundHi : kRadrSoundHi)[i]);
            for (int copy = 0; copy < 2; ++copy) std::copy(h.begin(), h.end(), r.sound.begin() + 0x100000 * (i + 1) + copy * 0x80000);
        }
    }
    r.tiles.assign(radm ? 0x200000 : 0x100000, 0);
    {
        static const int lane[4] = {0, 1, 2, 3}; // kRadrTiles is in lane order
        for (int f = 0; f < 4; ++f) {
            const auto d = src.get((radm ? kRadmTiles : kRadrTiles)[f]);
            for (size_t i = 0; i < d.size(); ++i) r.tiles[4 * i + size_t(lane[f])] = d[i];
        }
    }
    r.sprites.assign(0x800000, 0);
    for (int f = 0; f < 8; ++f) {
        const auto d = src.get(kRadrSprites[f]);
        for (size_t i = 0; i < d.size(); ++i) r.sprites[8 * i + size_t(f)] = d[i];
    }
    r.eeprom = src.get(radm ? kRadmEeprom : kRadrEeprom);
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
    if (set == kRadrRomSet) return import_radr(src, false);
    if (set == kRadmRomSet) return import_radr(src, true);
    if (set == kF1enRomSet) return import_f1en(src);
    if (set == kJparkRomSet) return import_jpark(src);
    if (set == kSonicRomSet) return import_sonic(src);
    if (set == kGa2RomSet) return import_ga2(src);
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
