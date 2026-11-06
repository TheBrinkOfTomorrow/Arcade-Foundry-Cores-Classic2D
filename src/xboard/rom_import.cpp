// license:BSD-3-Clause
// copyright-holders:Aaron Giles
//
// The file lists, CRCs and layouts are MAME's ROM_START( aburner2 ),
// ROM_START( aburner131 ), ROM_START( thndrbld ), ROM_START( thndrbld1 ),
// ROM_START( rachero ), ROM_START( smgp ) and ROM_START( loffire )
// (src/mame/sega/segaxbd.cpp).
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

// After Burner (VER 1.31): its own programs, tiles, last two sprite groups,
// sound program and third PCM ROM; the rest is After Burner II's
const File kAb1Main[2] = {{"epr-10940.58", 0x20000, 0x4d132c4e}, {"epr-10941.63", 0x20000, 0x136ea264}};
const File kAb1Sub[2] = {{"epr-10927.20", 0x20000, 0x66d36757}, {"epr-10928.29", 0x20000, 0x7c01d40b}};
const File kAb1Tiles[3] = {{"epr-10926.154", 0x10000, 0xed8bd632}, {"epr-10925.153", 0x10000, 0x4ef048cc}, {"epr-10924.152", 0x10000, 0x50c15a6d}};
const File kAb1Sprites[8] = {
    {"epr-10942.92", 0x20000, 0x5ce10b8c},  {"epr-10943.96", 0x20000, 0xb98294dc},  {"epr-10944.100", 0x20000, 0x17be8f67},
    {"epr-10945.104", 0x20000, 0xdf4d4c4f}, {"epr-10946.93", 0x20000, 0xd7d485f4},  {"epr-10947.97", 0x20000, 0x08838392},
    {"epr-10948.101", 0x20000, 0x64284761}, {"epr-10949.105", 0x20000, 0xd8437d92}};
const File kAb1Sound = {"epr-10923.17", 0x10000, 0x6888eb8f};
const File kAb1Pcm3 = {"epr-10929.13", 0x20000, 0x6c07c78d};

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

// Racing Hero (FD1094 317-0144): no road ROM (the region stays 0)
const File kRhMain[4] = {{"epr-13129.ic58", 0x20000, 0xad9f32e7}, {"epr-13130.ic63", 0x20000, 0x6022777b},
                         {"epr-12855.ic57", 0x20000, 0xcecf1e73}, {"epr-12856.ic62", 0x20000, 0xda900ebb}};
const File kRhKey = {"317-0144.key", 0x2000, 0x8740bbff};
const File kRhSub[2] = {{"epr-12857.ic20", 0x20000, 0x8a2328cc}, {"epr-12858.ic29", 0x20000, 0x38a248b7}};
const File kRhTiles[3] = {{"epr-12879.ic154", 0x10000, 0xc1a9de7a}, {"epr-12880.ic153", 0x10000, 0x27ff04a5},
                          {"epr-12881.ic152", 0x10000, 0x72f14491}};
const File kRhSprites[16] = {
    {"epr-12872.ic90", 0x20000, 0x68d56139},  {"epr-12873.ic94", 0x20000, 0x3d3ec450},  {"epr-12874.ic98", 0x20000, 0x7d6bde23},
    {"epr-12875.ic102", 0x20000, 0xe33092bf}, {"epr-12868.ic91", 0x20000, 0x96289583},  {"epr-12869.ic95", 0x20000, 0x2ef0de02},
    {"epr-12870.ic99", 0x20000, 0xc76630e1},  {"epr-12871.ic103", 0x20000, 0x23401b1a}, {"epr-12864.ic92", 0x20000, 0x77d6cff4},
    {"epr-12865.ic96", 0x20000, 0x1e7e685b},  {"epr-12866.ic100", 0x20000, 0xfdf31329}, {"epr-12867.ic104", 0x20000, 0xb25e37fd},
    {"epr-12860.ic93", 0x20000, 0x86b64119},  {"epr-12861.ic97", 0x20000, 0xbccff19b},  {"epr-12862.ic101", 0x20000, 0x7d4c3b05},
    {"epr-12863.ic105", 0x20000, 0x85095053}};
const File kRhSound = {"epr-12859.ic17", 0x10000, 0xd57881da};
const File kRhPcm[3] = {{"epr-12876.ic11", 0x20000, 0xf72a34a0}, {"epr-12877.ic12", 0x20000, 0x18c1b6d2},
                        {"epr-12878.ic13", 0x20000, 0x7c212c15}};

// Super Monaco GP (FD1094 317-0126a): no road ROM; the extra sound board
// has its own program and a copy of the PCM ROMs (the comm and motor boards'
// programs are not needed)
const File kSmMain[2] = {{"epr-12563b.58", 0x20000, 0xbaf1f333}, {"epr-12564b.63", 0x20000, 0xb5191af0}};
const File kSmKey = {"317-0126a.key", 0x2000, 0x2abc1982};
const File kSmSub[2] = {{"epr-12576a.20", 0x20000, 0x2c9599c1}, {"epr-12577a.29", 0x20000, 0xabf9a50b}};
const File kSmTiles[3] = {{"epr-12429.154", 0x10000, 0x5851e614}, {"epr-12430.153", 0x10000, 0x05e00134},
                          {"epr-12431.152", 0x10000, 0x35572f4a}};
const File kSmSprites[16] = {
    {"mpr-12425.90", 0x20000, 0x14bf2a15},  {"mpr-12426.94", 0x20000, 0x28b60dc0},  {"mpr-12427.98", 0x20000, 0x0a367928},
    {"mpr-12428.102", 0x20000, 0xefa80ad5}, {"mpr-12421.91", 0x20000, 0x25f46140},  {"mpr-12422.95", 0x20000, 0xcb51c8f6},
    {"mpr-12423.99", 0x20000, 0x0be9818e},  {"mpr-12424.103", 0x20000, 0x0ce00dfc}, {"mpr-12417.92", 0x20000, 0xa806eabf},
    {"mpr-12418.96", 0x20000, 0xed1a0f2b},  {"mpr-12419.100", 0x20000, 0xce4568cb}, {"mpr-12420.104", 0x20000, 0x679442eb},
    {"epr-12609.93", 0x20000, 0xa867812f},  {"epr-12610.97", 0x20000, 0x53b99417},  {"epr-12611.101", 0x20000, 0xbd5c6ab0},
    {"epr-12612.105", 0x20000, 0xac86e890}};
const File kSmSound = {"epr-12436.17", 0x10000, 0x16ec5f0a};
const File kSmPcm[3] = {{"mpr-12437.11", 0x20000, 0xa1c7e712}, {"mpr-12438.12", 0x20000, 0x6573d46b},
                        {"mpr-12439.13", 0x20000, 0x13bf6de5}};
const File kSmSound2 = {"epr-12535.8", 0x10000, 0x80453597};
const File kSmPcm2[3] = {{"mpr-12437.20", 0x20000, 0xa1c7e712}, {"mpr-12438.21", 0x20000, 0x6573d46b},
                         {"mpr-12439.22", 0x20000, 0x13bf6de5}};

std::vector<uint8_t> load_pcm(const rt::RomSource &src, const File *f) {
    std::vector<uint8_t> pcm(0x80000, 0xff);
    for (int i = 0; i < 3; ++i) {
        const auto d = src.get(f[i]);
        std::copy(d.begin(), d.end(), pcm.begin() + i * 0x20000);
    }
    return pcm;
}

// Line of Fire (World, FD1094 317-0136; MAME marks the key BAD_DUMP: one
// byte rebuilt, which decrypts all the code): no road ROM
const File kLfMain[2] = {{"epr-12849.58", 0x20000, 0x61cfd2fe}, {"epr-12850.63", 0x20000, 0x14598f2a}};
const File kLfKey = {"317-0136.key", 0x2000, 0x344bfe0c};
const File kLfSub[4] = {{"epr-12804.20", 0x20000, 0xb853480e}, {"epr-12805.29", 0x20000, 0x4a7200c3},
                        {"epr-12802.21", 0x20000, 0xd746bb39}, {"epr-12803.30", 0x20000, 0xc1d9e751}};
const File kLfTiles[3] = {{"opr-12791.154", 0x10000, 0xacfa69ba}, {"opr-12792.153", 0x10000, 0xe506723c},
                          {"opr-12793.152", 0x10000, 0x0ce8cce3}};
const File kLfSprites[16] = {
    {"epr-12787.90", 0x20000, 0x6431a3a6},  {"epr-12788.94", 0x20000, 0x1982a0ce},  {"epr-12789.98", 0x20000, 0x97d03274},
    {"epr-12790.102", 0x20000, 0x816e76e6}, {"epr-12783.91", 0x20000, 0xc13feea9},  {"epr-12784.95", 0x20000, 0x39b94c65},
    {"epr-12785.99", 0x20000, 0x05ed0059},  {"epr-12786.103", 0x20000, 0xa4123165}, {"epr-12779.92", 0x20000, 0xae58af7c},
    {"epr-12780.96", 0x20000, 0xee670c1e},  {"epr-12781.100", 0x20000, 0x538f6bc5}, {"epr-12782.104", 0x20000, 0x5acc34f7},
    {"epr-12775.93", 0x20000, 0x693056ec},  {"epr-12776.97", 0x20000, 0x61efbdfd},  {"epr-12777.101", 0x20000, 0x29d5b953},
    {"epr-12778.105", 0x20000, 0x2fb68e07}};
const File kLfSound = {"epr-12798.17", 0x10000, 0x0587738d};
const File kLfPcm[3] = {{"epr-12799.11", 0x20000, 0xbc60181c}, {"epr-12800.12", 0x20000, 0x1158c1a3},
                        {"epr-12801.13", 0x20000, 0x2d6567c4}};

Roms import_smgp(const rt::RomSource &src) {
    Roms r;
    r.game = Game::smgp;
    r.main = src.load16_byte(kSmMain, 1, 0x80000);
    r.fd1094_key = src.get(kSmKey);
    r.sub = src.load16_byte(kSmSub, 1, 0x80000);
    r.sound = src.get(kSmSound);
    r.pcm = load_pcm(src, kSmPcm);
    r.sound2 = src.get(kSmSound2);
    r.pcm2 = load_pcm(src, kSmPcm2);
    for (const File &f : kSmTiles) {
        const auto d = src.get(f);
        r.video.tiles.insert(r.video.tiles.end(), d.begin(), d.end());
    }
    r.video.sprites = src.load32_byte(kSmSprites, 4, 0x200000);
    r.video.road.assign(0x10000, 0);
    return r;
}

Roms import_loffire(const rt::RomSource &src) {
    Roms r;
    r.game = Game::loffire;
    r.main = src.load16_byte(kLfMain, 1, 0x80000);
    r.fd1094_key = src.get(kLfKey);
    r.sub = src.load16_byte(kLfSub, 2, 0x80000);
    r.sound = src.get(kLfSound);
    r.pcm = load_pcm(src, kLfPcm);
    for (const File &f : kLfTiles) {
        const auto d = src.get(f);
        r.video.tiles.insert(r.video.tiles.end(), d.begin(), d.end());
    }
    r.video.sprites = src.load32_byte(kLfSprites, 4, 0x200000);
    r.video.road.assign(0x10000, 0);
    return r;
}

// GP Rider (World, FD1094 317-0163): MAME's gpriders, one board of the
// twin set gprider (the same files): no road ROM
const File kGpMain[2] = {{"epr-13409.ic58", 0x20000, 0x9abb81b6}, {"epr-13408.ic63", 0x20000, 0x8e410e97}};
const File kGpKey = {"317-0163.key", 0x2000, 0xc1d4d207};
const File kGpSub[4] = {{"epr-13395.ic20", 0x20000, 0xd6ccfac7}, {"epr-13394.ic29", 0x20000, 0x914a55ec},
                        {"epr-13393.ic21", 0x20000, 0x08d023cc}, {"epr-13392.ic30", 0x20000, 0xf927cd42}};
const File kGpTiles[3] = {{"epr-13383.ic154", 0x10000, 0x24f897a7}, {"epr-13384.ic153", 0x10000, 0xfe8238bd},
                          {"epr-13385.ic152", 0x10000, 0x6df1b995}};
const File kGpSprites[16] = {
    {"epr-13382.ic90", 0x20000, 0x01dac209},  {"epr-13381.ic94", 0x20000, 0x3a50d931},  {"epr-13380.ic98", 0x20000, 0xad1024c8},
    {"epr-13379.ic102", 0x20000, 0x1ac17625}, {"epr-13378.ic91", 0x20000, 0x50c9b867},  {"epr-13377.ic95", 0x20000, 0x9b12f5c0},
    {"epr-13376.ic99", 0x20000, 0x449ac518},  {"epr-13375.ic103", 0x20000, 0x5489a9ff}, {"epr-13374.ic92", 0x20000, 0x6a319e4f},
    {"epr-13373.ic96", 0x20000, 0xeca5588b},  {"epr-13372.ic100", 0x20000, 0x0b45a433}, {"epr-13371.ic104", 0x20000, 0xb68f4cff},
    {"epr-13370.ic93", 0x20000, 0x78276620},  {"epr-13369.ic97", 0x20000, 0x8625bf0f},  {"epr-13368.ic101", 0x20000, 0x0f50716c},
    {"epr-13367.ic105", 0x20000, 0x4b1bb51f}};
const File kGpSound = {"epr-13388.ic17", 0x10000, 0x706581e4};
const File kGpPcm[3] = {{"epr-13391.ic11", 0x20000, 0x8c30c867}, {"epr-13390.ic12", 0x20000, 0x8c93cd05},
                        {"epr-13389.ic13", 0x20000, 0x4e4c758e}};

Roms import_gprider(const rt::RomSource &src) {
    Roms r;
    r.game = Game::gprider;
    r.main = src.load16_byte(kGpMain, 1, 0x80000);
    r.fd1094_key = src.get(kGpKey);
    r.sub = src.load16_byte(kGpSub, 2, 0x80000);
    r.sound = src.get(kGpSound);
    r.pcm = load_pcm(src, kGpPcm);
    for (const File &f : kGpTiles) {
        const auto d = src.get(f);
        r.video.tiles.insert(r.video.tiles.end(), d.begin(), d.end());
    }
    r.video.sprites = src.load32_byte(kGpSprites, 4, 0x200000);
    r.video.road.assign(0x10000, 0);
    return r;
}

Roms import_racing_hero(const rt::RomSource &src) {
    Roms r;
    r.game = Game::rachero;
    r.main = src.load16_byte(kRhMain, 2, 0x80000);
    r.fd1094_key = src.get(kRhKey);
    r.sub = src.load16_byte(kRhSub, 1, 0x80000);
    r.sound = src.get(kRhSound);
    r.pcm.assign(0x80000, 0xff);
    for (int i = 0; i < 3; ++i) {
        const auto d = src.get(kRhPcm[i]);
        std::copy(d.begin(), d.end(), r.pcm.begin() + i * 0x20000);
    }
    for (const File &f : kRhTiles) {
        const auto d = src.get(f);
        r.video.tiles.insert(r.video.tiles.end(), d.begin(), d.end());
    }
    r.video.sprites = src.load32_byte(kRhSprites, 4, 0x200000);
    r.video.road.assign(0x10000, 0);
    return r;
}

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
    if (game == Game::rachero) return import_racing_hero(src);
    if (game == Game::smgp) return import_smgp(src);
    if (game == Game::loffire) return import_loffire(src);
    if (game == Game::gprider) return import_gprider(src);
    if (!is_aburner(game)) return import_thunder_blade(src, game);
    const bool ab1 = game == Game::aburner131;
    Roms r;
    r.game = game;
    r.main = src.load16_byte(ab1 ? kAb1Main : kMain, 1, 0x80000);
    r.sub = src.load16_byte(ab1 ? kAb1Sub : kSub, 1, 0x80000);
    r.sound = src.get(ab1 ? kAb1Sound : kSound);
    r.pcm.assign(0x80000, 0xff);
    for (int i = 0; i < 3; ++i) {
        const auto d = src.get(ab1 && i == 2 ? kAb1Pcm3 : kPcm[i]);
        std::copy(d.begin(), d.end(), r.pcm.begin() + i * 0x20000);
    }
    for (const File &f : ab1 ? kAb1Tiles : kTiles) {
        const auto d = src.get(f);
        r.video.tiles.insert(r.video.tiles.end(), d.begin(), d.end());
    }
    File sprites[16];
    std::copy(std::begin(kSprites), std::end(kSprites), sprites);
    if (ab1) std::copy(std::begin(kAb1Sprites), std::end(kAb1Sprites), sprites + 8);
    r.video.sprites = src.load32_byte(sprites, 4, 0x200000);
    r.video.road = src.get(kRoad);
    return r;
}

} // namespace xbd
