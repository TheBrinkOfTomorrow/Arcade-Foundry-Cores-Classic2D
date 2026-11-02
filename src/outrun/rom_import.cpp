// license:BSD-3-Clause
// copyright-holders:Aaron Giles
//
// The file lists, CRCs and layouts are MAME's ROM_START( outrun ),
// ROM_START( shangon ) and ROM_START( toutrun ) (src/mame/sega/segaorun.cpp).
#include "outrun/rom_import.h"

#include <algorithm>

namespace orun {
namespace {

using File = rt::RomFile;

// ROM_LOAD16_BYTE pairs (even, odd) at 0x00000 and 0x20000
const File kMain[4] = {{"epr-10380b.133", 0x10000, 0x1f6cadad}, {"epr-10382b.118", 0x10000, 0xc4c3fa1a},
                       {"epr-10381b.132", 0x10000, 0xbe8c412b}, {"epr-10383b.117", 0x10000, 0x10a2014a}};
const File kSub[4] = {{"epr-10327a.76", 0x10000, 0xe28a5baf}, {"epr-10329a.58", 0x10000, 0xda131c81},
                      {"epr-10328a.75", 0x10000, 0xd5ec5e5d}, {"epr-10330a.57", 0x10000, 0xba9ec82a}};
const File kTiles[6] = {{"opr-10268.99", 0x8000, 0x95344b04},  {"opr-10232.102", 0x8000, 0x776ba1eb},
                        {"opr-10267.100", 0x8000, 0xa85bb823}, {"opr-10231.103", 0x8000, 0x8908bcbf},
                        {"opr-10266.101", 0x8000, 0x9f6f1a74}, {"opr-10230.104", 0x8000, 0x686f5e50}};
// ROM_LOAD32_BYTE: byte lanes 0-3 of the first half, then of the second
const File kSprites[8] = {{"mpr-10371.9", 0x20000, 0x7cc86208},  {"mpr-10373.10", 0x20000, 0xb0d26ac9},
                          {"mpr-10375.11", 0x20000, 0x59b60bd7}, {"mpr-10377.12", 0x20000, 0x17a1b04a},
                          {"mpr-10372.13", 0x20000, 0xb557078c}, {"mpr-10374.14", 0x20000, 0x8051e517},
                          {"mpr-10376.15", 0x20000, 0xf3b8f318}, {"mpr-10378.16", 0x20000, 0xa1062984}};
const File kRoad[2] = {{"opr-10186.47", 0x8000, 0x22794426}, {"opr-10185.11", 0x8000, 0x22794426}};
const File kSound = {"epr-10187.88", 0x8000, 0xa10abaa9};
// ROM_LOAD at n * 0x10000, ROM_RELOAD at + 0x8000, the rest 0xff
const File kPcm[6] = {{"opr-10193.66", 0x8000, 0xbcd10dde}, {"opr-10192.67", 0x8000, 0x770f1270},
                      {"opr-10191.68", 0x8000, 0x20a284ab}, {"opr-10190.69", 0x8000, 0x7cab70e2},
                      {"opr-10189.70", 0x8000, 0x01366b54}, {"opr-10188.71", 0x8000, 0xbad30ad9}};

// Super Hang-On (the unprotected parent set)
const File kShMain[4] = {{"epr-10886.133", 0x10000, 0x8be3cd36}, {"epr-10884.118", 0x10000, 0xcb06150d},
                         {"epr-10887.132", 0x10000, 0x8d248bb0}, {"epr-10885.117", 0x10000, 0x70795f26}};
const File kShSub[4] = {{"epr-10792.76", 0x10000, 0x16299d25}, {"epr-10790.58", 0x10000, 0x2246cbc1},
                        {"epr-10793.75", 0x10000, 0xd9525427}, {"epr-10791.57", 0x10000, 0x5faf4cbe}};
const File kShTiles[3] = {{"epr-10652.54", 0x8000, 0x260286f9}, {"epr-10651.55", 0x8000, 0xc609ee7b},
                          {"epr-10650.56", 0x8000, 0xb236a403}};
// ROM_REGION16_BE, ROM_LOAD16_BYTE pairs (even, odd) every 0x40000
const File kShSprites[8] = {{"mpr-10798.16", 0x20000, 0x7d58f807}, {"mpr-10794.8", 0x20000, 0x7c958e63},
                            {"mpr-10799.14", 0x20000, 0x96d90d3d}, {"mpr-10795.6", 0x20000, 0xd9d31f8c},
                            {"mpr-10800.12", 0x20000, 0xfeaff98e}, {"mpr-10796.4", 0x20000, 0xfb48957c},
                            {"mpr-10801.10", 0x20000, 0x12781795}, {"mpr-10797.2", 0x20000, 0x27f2870d}};
const File kShRoad = {"epr-10642.47", 0x8000, 0x7836bcc3}; // IC11 not populated
const File kShSound = {"epr-10649c.88", 0x8000, 0xf6c1ce71};
// ROM_LOAD at n * 0x10000, ROM_RELOAD at + 0x8000 (a 0x40000 region)
const File kShPcm[4] = {{"epr-10643.66", 0x8000, 0x06f55364}, {"epr-10644.67", 0x8000, 0xb41d541d},
                        {"epr-10645.68", 0x8000, 0xa60dabff}, {"epr-10646.69", 0x8000, 0x473cc411}};

// Turbo Out Run (Out Run upgrade, FD1094 317-0118)
const File kTrMain[6] = {{"epr-12513.133", 0x10000, 0xae8835a5}, {"epr-12512.118", 0x10000, 0xf90372ad},
                         {"epr-12515.132", 0x10000, 0x1f047df4}, {"epr-12514.117", 0x10000, 0x5539e9c3},
                         {"epr-12293.131", 0x10000, 0xf4321eea}, {"epr-12292.116", 0x10000, 0x51d98af0}};
const File kTrKey = {"317-0118.key", 0x2000, 0x083d7d56};
const File kTrSub[4] = {{"opr-12295.76", 0x10000, 0xd43a3a84}, {"opr-12294.58", 0x10000, 0x27cdcfd3},
                        {"opr-12297.75", 0x10000, 0x1d9b5677}, {"opr-12296.57", 0x10000, 0x0a513671}};
const File kTrTiles[3] = {{"opr-12323.102", 0x10000, 0x4de43a6f}, {"opr-12324.103", 0x10000, 0x24607a55},
                          {"opr-12325.104", 0x10000, 0x1405137a}};
const File kTrSprites[8] = {{"mpr-12336.9", 0x20000, 0xdda465c7},  {"mpr-12337.10", 0x20000, 0x828233d1},
                            {"mpr-12338.11", 0x20000, 0x46b4b5f4}, {"mpr-12339.12", 0x20000, 0x0d7e3bab},
                            {"mpr-12364.13", 0x20000, 0xa4b83e65}, {"mpr-12365.14", 0x20000, 0x4a80b2a9},
                            {"mpr-12366.15", 0x20000, 0x385cb3ab}, {"mpr-12367.16", 0x20000, 0x4930254a}};
const File kTrRoad[2] = {{"epr-12299.47", 0x8000, 0xfc9bc41b}, {"epr-12298.11", 0x8000, 0xfc9bc41b}};
const File kTrSound = {"epr-12300.88", 0x10000, 0xe8ff7011};
const File kTrPcm[6] = {{"opr-12301.66", 0x10000, 0x6e78ad15}, {"opr-12302.67", 0x10000, 0xe72928af},
                        {"opr-12303.68", 0x10000, 0x8384205c}, {"opr-12304.69", 0x10000, 0xe1762ac3},
                        {"opr-12305.70", 0x10000, 0xba9ce677}, {"opr-12306.71", 0x10000, 0xe49249fd}};

Roms import_toutrun(const rt::RomSource &src) {
    Roms r;
    r.game = Game::toutrun;
    r.main = src.load16_byte(kTrMain, 3, 0x60000);
    r.fd1094_key = src.get(kTrKey);
    r.sub = src.load16_byte(kTrSub, 2, 0x40000);
    r.sound = src.get(kTrSound);
    r.pcm.assign(0x80000, 0xff);
    for (int i = 0; i < 6; ++i) {
        const auto d = src.get(kTrPcm[i]);
        std::copy(d.begin(), d.end(), r.pcm.begin() + i * 0x10000);
    }
    for (const File &f : kTrTiles) {
        const auto d = src.get(f);
        r.video.tiles.insert(r.video.tiles.end(), d.begin(), d.end());
    }
    r.video.sprites = src.load32_byte(kTrSprites, 2, 0x100000);
    for (const File &f : kTrRoad) {
        const auto d = src.get(f);
        r.video.road.insert(r.video.road.end(), d.begin(), d.end());
    }
    return r;
}

Roms import_shangon(const rt::RomSource &src) {
    Roms r;
    r.game = Game::shangon;
    r.main = src.load16_byte(kShMain, 2, 0x40000);
    r.sub = src.load16_byte(kShSub, 2, 0x40000);
    r.sound = src.get(kShSound);
    // the 0x40000 region, mirrored through the 315-5218's 0x80000 space
    r.pcm.assign(0x80000, 0xff);
    for (int i = 0; i < 4; ++i) {
        const auto d = src.get(kShPcm[i]);
        for (uint32_t at : {0x00000u, 0x08000u, 0x40000u, 0x48000u}) std::copy(d.begin(), d.end(), r.pcm.begin() + i * 0x10000 + at);
    }
    for (const File &f : kShTiles) {
        const auto d = src.get(f);
        r.video.tiles.insert(r.video.tiles.end(), d.begin(), d.end());
    }
    r.video.sprites = src.load16_byte(kShSprites, 4, 0x100000);
    r.video.road = src.get(kShRoad);
    r.video.road.resize(0x10000, 0);
    return r;
}

} // namespace

Roms import_rom_set(const std::string &path, Game game) {
    const rt::RomSource src(path);
    if (game == Game::shangon) return import_shangon(src);
    if (game == Game::toutrun) return import_toutrun(src);
    Roms r;
    r.main = src.load16_byte(kMain, 2, 0x40000);
    r.sub = src.load16_byte(kSub, 2, 0x40000);
    r.sound = src.get(kSound);
    r.pcm.assign(0x80000, 0xff);
    for (int i = 0; i < 6; ++i) {
        const auto d = src.get(kPcm[i]);
        std::copy(d.begin(), d.end(), r.pcm.begin() + i * 0x10000);
        std::copy(d.begin(), d.end(), r.pcm.begin() + i * 0x10000 + 0x8000);
    }
    for (const File &f : kTiles) {
        const auto d = src.get(f);
        r.video.tiles.insert(r.video.tiles.end(), d.begin(), d.end());
    }
    r.video.sprites = src.load32_byte(kSprites, 2, 0x100000);
    for (const File &f : kRoad) {
        const auto d = src.get(f);
        r.video.road.insert(r.video.road.end(), d.begin(), d.end());
    }
    return r;
}

} // namespace orun
