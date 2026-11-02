// license:BSD-3-Clause
// copyright-holders:Aaron Giles
//
// The file lists, CRCs and layouts are MAME's ROM_START( aburner2 ),
// ROM_START( thndrbld ) and ROM_START( thndrbld1 ) (src/mame/sega/segaxbd.cpp).
#include "xboard/rom_import.h"

#include <algorithm>

namespace xbd {
namespace {

using File = rt::RomFile;

// ROM_LOAD16_BYTE (even, odd)
const File kMain[2] = {{"epr-11107.58", 0x20000, 0x6d87bab7}, {"epr-11108.63", 0x20000, 0x202a3e1d}};
const File kSub[2] = {{"epr-11109.20", 0x20000, 0x85a0fe07}, {"epr-11110.29", 0x20000, 0xf3d6797c}};
const File kTiles[3] = {{"epr-11115.154", 0x10000, 0xe8e32921}, {"epr-11114.153", 0x10000, 0x2e97f633}, {"epr-11113.152", 0x10000, 0x36058c8c}};
// ROM_LOAD32_BYTE: byte lanes 0-3, four groups
const File kSprites[16] = {
    {"mpr-10932.90", 0x20000, 0xcc0821d6},  {"mpr-10934.94", 0x20000, 0x4a51b1fa},  {"mpr-10936.98", 0x20000, 0xada70d64},
    {"mpr-10938.102", 0x20000, 0xe7675baf}, {"mpr-10933.91", 0x20000, 0xc8efb2c3},  {"mpr-10935.95", 0x20000, 0xc1e23521},
    {"mpr-10937.99", 0x20000, 0xf0199658},  {"mpr-10939.103", 0x20000, 0xa0d49480}, {"epr-11103.92", 0x20000, 0xbdd60da2},
    {"epr-11104.96", 0x20000, 0x06a35fce},  {"epr-11105.100", 0x20000, 0x027b0689}, {"epr-11106.104", 0x20000, 0x9e1fec09},
    {"epr-11116.93", 0x20000, 0x49b4c1ba},  {"epr-11117.97", 0x20000, 0x821fbb71},  {"epr-11118.101", 0x20000, 0x8f38540b},
    {"epr-11119.105", 0x20000, 0xd0343a8e}};
const File kRoad = {"epr-10922.40", 0x10000, 0xb49183d4};
const File kSound = {"epr-11112.17", 0x10000, 0xd777fc6d};
// ROM_LOAD at n * 0x20000, the rest 0xff
const File kPcm[3] = {{"mpr-10931.11", 0x20000, 0x9209068f}, {"mpr-10930.12", 0x20000, 0x6493368b}, {"epr-11102.13", 0x20000, 0x6c07c78d}};

// Thunder Blade: thndrbld (upright, FD1094 317-0056) and thndrbld1
// (deluxe/standing, unprotected) share everything but the programs, the
// last sprite group and the sound program
const File kTbMain[4] = {{"epr-11405.ic58", 0x20000, 0xe057dd5a}, {"epr-11406.ic63", 0x20000, 0xc6b994b8},
                         {"epr-11306.ic57", 0x20000, 0x4b95f2b4}, {"epr-11307.ic62", 0x20000, 0x2d6833e4}};
const File kTbKey = {"317-0056.key", 0x2000, 0xb40cd2c5};
const File kTbSub[4] = {{"epr-11390.ic20", 0x20000, 0xed988fdb}, {"epr-11391.ic29", 0x20000, 0x12523bc1},
                        {"epr-11310.ic21", 0x20000, 0x5d9fa02c}, {"epr-11311.ic30", 0x20000, 0x483de21b}};
const File kTb1Main[4] = {{"epr-11304.ic58", 0x20000, 0xa90630ef}, {"epr-11305.ic63", 0x20000, 0x9ba3ef61},
                          {"epr-11306.ic57", 0x20000, 0x4b95f2b4}, {"epr-11307.ic62", 0x20000, 0x2d6833e4}};
const File kTb1Sub[4] = {{"epr-11308.ic20", 0x20000, 0x7956c238}, {"epr-11309.ic29", 0x20000, 0xc887f620},
                         {"epr-11310.ic21", 0x20000, 0x5d9fa02c}, {"epr-11311.ic30", 0x20000, 0x483de21b}};
const File kTbTiles[3] = {{"epr-11314.ic154", 0x10000, 0xd4f954a9}, {"epr-11315.ic153", 0x10000, 0x35813088},
                          {"epr-11316.ic152", 0x10000, 0x84290dff}};
const File kTbSprites[12] = {
    {"epr-11323.ic90", 0x20000, 0x27e40735},  {"epr-11322.ic94", 0x20000, 0x10364d74},  {"epr-11321.ic98", 0x20000, 0x8e738f58},
    {"epr-11320.ic102", 0x20000, 0xa95c76b8}, {"epr-11327.ic91", 0x20000, 0xdeae90f1},  {"epr-11326.ic95", 0x20000, 0x29198403},
    {"epr-11325.ic99", 0x20000, 0xb9e98ae9},  {"epr-11324.ic103", 0x20000, 0x9742b552}, {"epr-11331.ic92", 0x20000, 0x3a2c042e},
    {"epr-11330.ic96", 0x20000, 0xaa7c70c5},  {"epr-11329.ic100", 0x20000, 0x31b20257}, {"epr-11328.ic104", 0x20000, 0xda39e89c}};
const File kTbSprites4[4] = {{"epr-11395.ic93", 0x20000, 0x90775579}, {"epr-11394.ic97", 0x20000, 0x5f2783be},
                             {"epr-11393.ic101", 0x20000, 0x525e2e1d}, {"epr-11392.ic105", 0x20000, 0xb4a382f7}};
const File kTb1Sprites4[4] = {{"epr-11335.ic93", 0x20000, 0xf19b3e86}, {"epr-11334.ic97", 0x20000, 0x348f91c7},
                              {"epr-11333.ic101", 0x20000, 0x05a2333f}, {"epr-11332.ic105", 0x20000, 0xdc089ec6}};
const File kTbRoad = {"epr-11313.ic29", 0x10000, 0x6a56c4c3};
const File kTbSound = {"epr-11396.ic17", 0x10000, 0xd37b54a4};
const File kTb1Sound = {"epr-11312.ic17", 0x10000, 0x3b974ed2};
const File kTbPcm[3] = {{"epr-11317.ic11", 0x20000, 0xd4e7ac1f}, {"epr-11318.ic12", 0x20000, 0x70d3f02c},
                        {"epr-11319.ic13", 0x20000, 0x50d9242e}};

Roms import_thunder_blade(const rt::RomSource &src, Game game) {
    const bool fd = game == Game::thndrbld;
    Roms r;
    r.game = game;
    r.main = src.load16_byte(fd ? kTbMain : kTb1Main, 2, 0x80000);
    if (fd) r.fd1094_key = src.get(kTbKey);
    r.sub = src.load16_byte(fd ? kTbSub : kTb1Sub, 2, 0x80000);
    r.sound = src.get(fd ? kTbSound : kTb1Sound);
    r.pcm.assign(0x80000, 0xff);
    for (int i = 0; i < 3; ++i) {
        const auto d = src.get(kTbPcm[i]);
        std::copy(d.begin(), d.end(), r.pcm.begin() + i * 0x20000);
    }
    for (const File &f : kTbTiles) {
        const auto d = src.get(f);
        r.video.tiles.insert(r.video.tiles.end(), d.begin(), d.end());
    }
    File sprites[16];
    std::copy(std::begin(kTbSprites), std::end(kTbSprites), sprites);
    const File *last = fd ? kTbSprites4 : kTb1Sprites4;
    std::copy(last, last + 4, sprites + 12);
    r.video.sprites = src.load32_byte(sprites, 4, 0x200000);
    r.video.road = src.get(kTbRoad);
    return r;
}

} // namespace

Roms import_rom_set(const std::string &path, Game game) {
    const rt::RomSource src(path);
    if (game != Game::aburner2) return import_thunder_blade(src, game);
    Roms r;
    r.main = src.load16_byte(kMain, 1, 0x80000);
    r.sub = src.load16_byte(kSub, 1, 0x80000);
    r.sound = src.get(kSound);
    r.pcm.assign(0x80000, 0xff);
    for (int i = 0; i < 3; ++i) {
        const auto d = src.get(kPcm[i]);
        std::copy(d.begin(), d.end(), r.pcm.begin() + i * 0x20000);
    }
    for (const File &f : kTiles) {
        const auto d = src.get(f);
        r.video.tiles.insert(r.video.tiles.end(), d.begin(), d.end());
    }
    r.video.sprites = src.load32_byte(kSprites, 4, 0x200000);
    r.video.road = src.get(kRoad);
    return r;
}

} // namespace xbd
