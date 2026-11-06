// license:BSD-3-Clause
// copyright-holders:Aaron Giles
//
// The file lists, CRCs and layouts are MAME's ROM_START( hangon ),
// ROM_START( sharrier ) and ROM_START( enduror ) (src/mame/sega/segahang.cpp). The PLD dumps are
// optional there and not read here.
#include "hangon/rom_import.h"

#include "fd1089/fd1089.h"

#include <algorithm>

namespace hang {
namespace {

using File = rt::RomFile;

// ROM_LOAD16_BYTE pairs (even, odd)
const File kMain[4] = {{"epr-6918a.ic22", 0x8000, 0x20b1c2b0}, {"epr-6916a.ic8", 0x8000, 0x7d9db1bf},
                       {"epr-6917a.ic20", 0x8000, 0xfea12367}, {"epr-6915a.ic6", 0x8000, 0xac883240}};
const File kSub[2] = {{"epr-6920.ic63", 0x8000, 0x1c95013e}, {"epr-6919.ic51", 0x8000, 0x6ca30d69}};
const File kTiles[3] = {{"epr-6841.ic38", 0x8000, 0x54d295dc}, {"epr-6842.ic23", 0x8000, 0xf677b568},
                        {"epr-6843.ic7", 0x8000, 0xa257f0da}};
// ROM_REGION16_BE: ROM_LOAD16_BYTE pairs (even, odd) every 0x10000
const File kSprites[14] = {{"epr-6820.ic34", 0x8000, 0x87cbc6de}, {"epr-6819.ic27", 0x8000, 0x469dad07},
                           {"epr-6822.ic35", 0x8000, 0xe9718de5}, {"epr-6821.ic28", 0x8000, 0x15792969},
                           {"epr-6824.ic36", 0x8000, 0x701deaa4}, {"epr-6823.ic29", 0x8000, 0x49422691},
                           {"epr-6826.ic37", 0x8000, 0x77d0de2c}, {"epr-6825.ic30", 0x8000, 0x6e23c8b4},
                           {"epr-6828.ic38", 0x8000, 0x8e880c93}, {"epr-6827.ic31", 0x8000, 0x7fa1bfb6},
                           {"epr-6830.ic39", 0x8000, 0xb1a63aef}, {"epr-6829.ic32", 0x8000, 0x7ca0952d},
                           {"epr-6846.ic25", 0x8000, 0xf21e57a3}, {"epr-6845.ic18", 0x8000, 0xba08c9b8}};
const File kRoad = {"epr-6840.ic108", 0x8000, 0x581230e3};
const File kSound = {"epr-6833.ic73", 0x4000, 0x3b942f5f};
const File kPcm[2] = {{"epr-6831.ic5", 0x8000, 0xcfef5481}, {"epr-6832.ic6", 0x8000, 0x4165aea5}};
const File kZoom = {"epr-6844.ic123", 0x2000, 0xe3ec7bd6};


// ---- Space Harrier: MAME's ROM_START( sharrier ) ----
namespace sh {
const File kMain[8] = {{"epr-7188a.ic97", 0x8000, 0x45e173c3}, {"epr-7184a.ic84", 0x8000, 0xe1934a51},
    {"epr-7189.ic98", 0x8000, 0x40b1309f}, {"epr-7185.ic85", 0x8000, 0xce78045c},
    {"epr-7190.ic99", 0x8000, 0xf6391091}, {"epr-7186.ic86", 0x8000, 0x79b367d7},
    {"epr-7191.ic100", 0x8000, 0x6171e9d3}, {"epr-7187.ic87", 0x8000, 0x70cb72ef}};
const File kSub[2] = {{"epr-7182.ic54", 0x8000, 0xd7c535b6}, {"epr-7183.ic67", 0x8000, 0xa6153af8}};
const File kTiles[3] = {{"epr-7196.ic31", 0x8000, 0x347fa325}, {"epr-7197.ic46", 0x8000, 0x39d98bd1}, {"epr-7198.ic60", 0x8000, 0x3da3ea6b}};
// ROM_REGION32_LE: ROM_LOAD32_BYTE groups of four lanes every 0x20000
const File kSprites[32] = {{"epr-7230.ic36", 0x8000, 0x93e2d264}, {"epr-7222.ic28", 0x8000, 0xedbf5fc3}, {"epr-7214.ic18", 0x8000, 0xe8c537d8}, {"epr-7206.ic8", 0x8000, 0x22844fa4},
    {"epr-7229.ic35", 0x8000, 0xcd6e7500}, {"epr-7221.ic27", 0x8000, 0x41f25a9c}, {"epr-7213.ic17", 0x8000, 0x5bb09a67}, {"epr-7205.ic7", 0x8000, 0xdcaa2ebf},
    {"epr-7228.ic34", 0x8000, 0xd5e15e66}, {"epr-7220.ic26", 0x8000, 0xac62ae2e}, {"epr-7212.ic16", 0x8000, 0x9c782295}, {"epr-7204.ic6", 0x8000, 0x3711105c},
    {"epr-7227.ic33", 0x8000, 0x60d7c1bb}, {"epr-7219.ic25", 0x8000, 0xf6330038}, {"epr-7211.ic15", 0x8000, 0x60737b98}, {"epr-7203.ic5", 0x8000, 0x70fb5ebb},
    {"epr-7226.ic32", 0x8000, 0x6d7b5c97}, {"epr-7218.ic24", 0x8000, 0xcebf797c}, {"epr-7210.ic14", 0x8000, 0x24596a8b}, {"epr-7202.ic4", 0x8000, 0xb537d082},
    {"epr-7225.ic31", 0x8000, 0x5e784271}, {"epr-7217.ic23", 0x8000, 0x510e5e10}, {"epr-7209.ic13", 0x8000, 0x7a2dad15}, {"epr-7201.ic3", 0x8000, 0xf5ba4e08},
    {"epr-7224.ic30", 0x8000, 0xec42c9ef}, {"epr-7216.ic22", 0x8000, 0x6d4a7d7a}, {"epr-7208.ic12", 0x8000, 0x0f732717}, {"epr-7200.ic2", 0x8000, 0xfc3bf8f3},
    {"epr-7223.ic29", 0x8000, 0xed51fdc4}, {"epr-7215.ic21", 0x8000, 0xdfe75f3d}, {"epr-7207.ic11", 0x8000, 0xa2c07741}, {"epr-7199.ic1", 0x8000, 0xb191e22f}};
const File kRoad = {"epr-7181.ic2", 0x8000, 0xb4740419};
const File kSound[2] = {{"epr-7234.ic73", 0x4000, 0xd6397933}, {"epr-7233.ic72", 0x4000, 0x504e76d9}};
const File kPcm[2] = {{"epr-7231.ic5", 0x8000, 0x871c6b14}, {"epr-7232.ic6", 0x8000, 0x4b59340c}};
const File kMcu = {"315-5163a.ic32", 0x1000, 0x203dffeb};
const File kZoom = {"epr-6844.ic123", 0x2000, 0xe3ec7bd6};
} // namespace sh

Roms import_sharrier(const rt::RomSource &src) {
    Roms r;
    r.game = Game::sharrier;
    r.main = src.load16_byte(sh::kMain, 4, 0x40000);
    r.sub = src.load16_byte(sh::kSub, 1, 0x40000);
    for (const File &f : sh::kSound) {
        const auto d = src.get(f);
        r.sound.insert(r.sound.end(), d.begin(), d.end());
    }
    r.sound.resize(0x10000, 0);
    for (const File &f : sh::kPcm) {
        const auto d = src.get(f);
        r.pcm.insert(r.pcm.end(), d.begin(), d.end());
    }
    r.mcu = src.get(sh::kMcu);
    for (const File &f : sh::kTiles) {
        const auto d = src.get(f);
        r.video.tiles.insert(r.video.tiles.end(), d.begin(), d.end());
    }
    r.video.sprites = src.load32_byte(sh::kSprites, 8, 0x100000);
    r.video.road = src.get(sh::kRoad);
    r.video.zoom = src.get(sh::kZoom);
    return r;
}


// ---- Enduro Racer: MAME's ROM_START( enduror ) (Rev A, YM2151, FD1089B 317-0013A) ----
namespace en {
const File kMain[6] = {{"epr-7640a.ic97", 0x8000, 0x1d1dc5d4}, {"epr-7636a.ic84", 0x8000, 0x84131639}, {"epr-7641.ic98", 0x8000, 0x2503ae7c}, {"epr-7637.ic85", 0x8000, 0x82a27a8c}, {"epr-7642.ic99", 0x8000, 0x1c453bea}, {"epr-7638.ic86", 0x8000, 0x70544779}};
const File kSub[2] = {{"epr-7634a.ic54", 0x8000, 0xaec83731}, {"epr-7635a.ic67", 0x8000, 0xb2fce96f}};
const File kTiles[3] = {{"epr-7644.ic31", 0x08000, 0xe7a4ff90}, {"epr-7645.ic46", 0x08000, 0x4caa0095}, {"epr-7646.ic60", 0x08000, 0x7e432683}};
const File kSprites[32] = {{"epr-7678.ic36", 0x8000, 0x9fb5e656}, {"epr-7670.ic28", 0x8000, 0xdbbe2f6e}, {"epr-7662.ic18", 0x8000, 0xcb0c13c5}, {"epr-7654.ic8", 0x8000, 0x2db6520d}, {"epr-7677.ic35", 0x8000, 0x7764765b}, {"epr-7669.ic27", 0x8000, 0xf9525faa}, {"epr-7661.ic17", 0x8000, 0xfe93a79b}, {"epr-7653.ic7", 0x8000, 0x46a52114}, {"epr-7676.ic34", 0x8000, 0x2e42e0d4}, {"epr-7668.ic26", 0x8000, 0xe115ce33}, {"epr-7660.ic16", 0x8000, 0x86dfbb68}, {"epr-7652.ic6", 0x8000, 0x2880cfdb}, {"epr-7675.ic33", 0x8000, 0x05cd2d61}, {"epr-7667.ic25", 0x8000, 0x923bde9d}, {"epr-7659.ic15", 0x8000, 0x629dc8ce}, {"epr-7651.ic5", 0x8000, 0xd7902bad}, {"epr-7674.ic32", 0x8000, 0x1a129acf}, {"epr-7666.ic24", 0x8000, 0x23697257}, {"epr-7658.ic14", 0x8000, 0x1677f24f}, {"epr-7650.ic4", 0x8000, 0x642635ec}, {"epr-7673.ic31", 0x8000, 0x82602394}, {"epr-7665.ic23", 0x8000, 0x12d77607}, {"epr-7657.ic13", 0x8000, 0x8158839c}, {"epr-7649.ic3", 0x8000, 0x4edba14c}, {"epr-7672.ic30", 0x8000, 0xd11452f7}, {"epr-7664.ic22", 0x8000, 0x0df2cfad}, {"epr-7656.ic12", 0x8000, 0x6c741272}, {"epr-7648.ic2", 0x8000, 0x983ea830}, {"epr-7671.ic29", 0x8000, 0xb0c7fdc6}, {"epr-7663.ic21", 0x8000, 0x2b0b8f08}, {"epr-7655.ic11", 0x8000, 0x3433fe7b}, {"epr-7647.ic1", 0x8000, 0x2e7fbec0}};
const File kRoad = {"epr-7633.ic1", 0x8000, 0x6f146210};
const File kSound = {"epr-7682.ic58", 0x8000, 0xc4efbf48};
// ROM_LOAD at 0 and 0x10000 of the 0x20000 region
const File kPcm[2] = {{"epr-7681.ic8", 0x8000, 0xbc0c4d12}, {"epr-7680.ic7", 0x8000, 0x627b3c8c}};
const File kZoom = {"epr-6844.ic123", 0x2000, 0xe3ec7bd6};
const File kKey = {"317-0013a.key", 0x2000, 0xa965b2da};
} // namespace en

Roms import_enduror(const rt::RomSource &src) {
    Roms r;
    r.game = Game::enduror;
    // the FD1089B: decrypted once for instruction fetches, once for data
    const std::vector<uint8_t> enc = src.load16_byte(en::kMain, 3, 0x40000);
    fd1089::decrypt(enc, src.get(en::kKey), fd1089::Variant::b, r.main_op, r.main);
    r.sub = src.load16_byte(en::kSub, 1, 0x40000);
    r.sound = src.get(en::kSound);
    r.sound.resize(0x10000, 0);
    r.pcm.assign(0x20000, 0);
    for (int i = 0; i < 2; ++i) {
        const auto p = src.get(en::kPcm[i]);
        std::copy(p.begin(), p.end(), r.pcm.begin() + i * 0x10000);
    }
    for (const File &f : en::kTiles) {
        const auto t = src.get(f);
        r.video.tiles.insert(r.video.tiles.end(), t.begin(), t.end());
    }
    r.video.sprites = src.load32_byte(en::kSprites, 8, 0x100000);
    r.video.road = src.get(en::kRoad);
    r.video.zoom = src.get(en::kZoom);
    return r;
}

} // namespace

Roms import_rom_set(const std::string &path, Game game) {
    const rt::RomSource src(path);
    if (game == Game::sharrier) return import_sharrier(src);
    if (game == Game::enduror) return import_enduror(src);
    Roms r;
    r.main = src.load16_byte(kMain, 2, 0x40000);
    r.sub = src.load16_byte(kSub, 1, 0x40000);
    r.sound = src.get(kSound);
    r.sound.resize(0x10000, 0);
    for (const File &f : kPcm) {
        const auto d = src.get(f);
        r.pcm.insert(r.pcm.end(), d.begin(), d.end());
    }
    for (const File &f : kTiles) {
        const auto d = src.get(f);
        r.video.tiles.insert(r.video.tiles.end(), d.begin(), d.end());
    }
    r.video.sprites = src.load16_byte(kSprites, 7, 0x80000);
    r.video.road = src.get(kRoad);
    r.video.zoom = src.get(kZoom);
    return r;
}

} // namespace hang
