// license:BSD-3-Clause
// copyright-holders:Aaron Giles
//
// The file lists, CRCs and layouts are MAME's ROM_START( gforce2 ) and
// ROM_START( pdrift ), ROM_START( gloc ), ROM_START( strkfgtr ) and
// ROM_START( rchase ) (src/mame/sega/segaybd.cpp).
#include "yboard/rom_import.h"

#include <algorithm>

namespace ybd {
namespace {

using File = rt::RomFile;

struct SetFiles {
    File main[4];
    int main_pairs;
    File subx[2], suby[2];
    File bsprites[4];
    File ysprites[32];
    File sound;
    File pcm[3]; // at 0, 0x80000 and 0x100000 (a 0x20000 ROM is reloaded to fill its 0x80000)
};

const SetFiles kGforce2 = {
    {{"epr-11688.25", 0x20000, 0xc845f2df}, {"epr-11687.24", 0x20000, 0x1cbefbbf}, {}, {}},
    1,
    {{"epr-11875.81", 0x20000, 0xc81701c6}, {"epr-11874.80", 0x20000, 0x5301fd79}},
    {{"epr-11816b.54", 0x20000, 0x317dd0c2}, {"epr-11815b.53", 0x20000, 0xf1fb22f1}},
    {{"mpr-11467.16", 0x20000, 0x6e60e736},
     {"mpr-11468.14", 0x20000, 0x74ca9ca5},
     {"epr-11694.17", 0x20000, 0x7e297b84},
     {"epr-11695.15", 0x20000, 0x38a864be}},
    {{"mpr-11469.67", 0x20000, 0xed7a2299}, {"mpr-11470.75", 0x20000, 0x34dea550},
     {"mpr-11477.63", 0x20000, 0xa2784653}, {"mpr-11478.71", 0x20000, 0x8b778993},
     {"mpr-11471.86", 0x20000, 0xf1974069}, {"mpr-11472.114", 0x20000, 0x0d24409a},
     {"mpr-11479.82", 0x20000, 0xecd6138a}, {"mpr-11480.110", 0x20000, 0x64ad66c5},
     {"mpr-11473.66", 0x20000, 0x0538c6ec}, {"mpr-11474.74", 0x20000, 0xeb923c50},
     {"mpr-11481.62", 0x20000, 0x78e652b6}, {"mpr-11482.70", 0x20000, 0x2f879766},
     {"mpr-11475.85", 0x20000, 0x69cfec89}, {"mpr-11476.113", 0x20000, 0xa60b9b79},
     {"mpr-11483.81", 0x20000, 0xd5d3a505}, {"mpr-11484.109", 0x20000, 0xb8a56a50},
     {"epr-11696.65", 0x20000, 0x99e8e49e}, {"epr-11697.73", 0x20000, 0x7545c52e},
     {"epr-11700.61", 0x20000, 0xe13839c1}, {"epr-11701.69", 0x20000, 0x9fb3d365},
     {"epr-11698.84", 0x20000, 0xcfeba3e2}, {"epr-11699.112", 0x20000, 0x4a00534a},
     {"epr-11702.80", 0x20000, 0x2a09c627}, {"epr-11703.108", 0x20000, 0x43bb7d9f},
     {"epr-11524.64", 0x20000, 0x5d35849f}, {"epr-11525.72", 0x20000, 0x9ae47552},
     {"epr-11532.60", 0x20000, 0xb3565ddb}, {"epr-11533.68", 0x20000, 0xf5d16e8a},
     {"epr-11526.83", 0x20000, 0x094cb3f0}, {"epr-11527.111", 0x20000, 0xe821a144},
     {"epr-11534.79", 0x20000, 0xb7f0ad7c}, {"epr-11535.107", 0x20000, 0x95da7a46}},
    {"epr-11693.102", 0x10000, 0x0497785c},
    {{"mpr-11465.107", 0x80000, 0xe1436dab},
     {"epr-11516.106", 0x20000, 0x19d0e17f},
     {"epr-11814.105", 0x20000, 0x0b05d376}},
};

const SetFiles kPdrift = {
    {{"epr-12017.25", 0x20000, 0x31190322},
     {"epr-12016.24", 0x20000, 0x499f64a6},
     {"epr-11748.27", 0x20000, 0x82a76cab},
     {"epr-11747.26", 0x20000, 0x9796ece5}},
    2,
    {{"epr-11905.81", 0x20000, 0x1cf68109}, {"epr-11904.80", 0x20000, 0xbb993681}},
    {{"epr-12019a.54", 0x20000, 0x11188a30}, {"epr-12018a.53", 0x20000, 0x1c582e1f}},
    {{"epr-11789.16", 0x20000, 0xb86f8d2b},
     {"epr-11791.14", 0x20000, 0x36b2910a},
     {"epr-11790.17", 0x20000, 0x2a564e66},
     {"epr-11792.15", 0x20000, 0xc85caf6e}},
    {{"epr-11757.67", 0x20000, 0xe46dc478}, {"epr-11758.75", 0x20000, 0x5b435c87},
     {"epr-11773.63", 0x20000, 0x1b5d5758}, {"epr-11774.71", 0x20000, 0x2ca0c170},
     {"epr-11759.86", 0x20000, 0xac8111f6}, {"epr-11760.114", 0x20000, 0x91282af9},
     {"epr-11775.82", 0x20000, 0x48225793}, {"epr-11776.110", 0x20000, 0x78c46198},
     {"epr-11761.66", 0x20000, 0xbaa5d065}, {"epr-11762.74", 0x20000, 0x1d1af7a5},
     {"epr-11777.62", 0x20000, 0x9662dd32}, {"epr-11778.70", 0x20000, 0x2dfb7494},
     {"epr-11763.85", 0x20000, 0x1ee23407}, {"epr-11764.113", 0x20000, 0xe859305e},
     {"epr-11779.81", 0x20000, 0xa49cd793}, {"epr-11780.109", 0x20000, 0xd514ed81},
     {"epr-11765.65", 0x20000, 0x649e2dff}, {"epr-11766.73", 0x20000, 0xd92fb7fc},
     {"epr-11781.61", 0x20000, 0x9692d4cd}, {"epr-11782.69", 0x20000, 0xc913bb43},
     {"epr-11767.84", 0x20000, 0x1f8ad054}, {"epr-11768.112", 0x20000, 0xdb2c4053},
     {"epr-11783.80", 0x20000, 0x6d189007}, {"epr-11784.108", 0x20000, 0x57f5fd64},
     {"epr-11769.64", 0x20000, 0x28f0ab51}, {"epr-11770.72", 0x20000, 0xd7557ea9},
     {"epr-11785.60", 0x20000, 0xe6ef32c4}, {"epr-11786.68", 0x20000, 0x2066b49d},
     {"epr-11771.83", 0x20000, 0x67635618}, {"epr-11772.111", 0x20000, 0x0f798d3a},
     {"epr-11787.79", 0x20000, 0xe631dc12}, {"epr-11788.107", 0x20000, 0x8464c66e}},
    {"epr-11899.102", 0x10000, 0xed9fa889},
    {{"mpr-11754.107", 0x80000, 0xebeb8484},
     {"epr-11756.106", 0x20000, 0x12e43f8a},
     {"epr-11755.105", 0x20000, 0xc2db1244}},
};

const SetFiles kGloc = {
    {{"epr-13172.25", 0x20000, 0x75e7174c},
     {"epr-13171.24", 0x20000, 0x90733468},
     {"epr-13028.27", 0x20000, 0xb6aa2edf},
     {"epr-13027.26", 0x20000, 0x6463c87a}},
    2,
    {{"epr-13032.81", 0x20000, 0x7da09c4e}, {"epr-13031.80", 0x20000, 0xf3c7e3f4}},
    {{"epr-13030.54", 0x20000, 0x81abcabf}, {"epr-13029.53", 0x20000, 0xf3638efb}},
    {{"epr-13039.16", 0x80000, 0xd7e1266d},
     {"epr-13037.14", 0x80000, 0xb801a250},
     {"epr-13040.17", 0x80000, 0x4aeb3a85},
     {"epr-13038.15", 0x80000, 0x0b2edb6d}},
    {{"epr-13048.67", 0x80000, 0xfe1eb0dd}, {"epr-13056.75", 0x80000, 0x5904f8e6},
     {"epr-13044.63", 0x80000, 0x4d931f89}, {"epr-13052.71", 0x80000, 0x0291f040},
     {"epr-13064.86", 0x80000, 0x5f8e651b}, {"epr-13072.114", 0x80000, 0x6b85641a},
     {"epr-13060.82", 0x80000, 0xee16ad97}, {"epr-13068.110", 0x80000, 0x64d52bbb},
     {"epr-13047.66", 0x80000, 0x53340832}, {"epr-13055.74", 0x80000, 0x39b6b665},
     {"epr-13043.62", 0x80000, 0x208f16fd}, {"epr-13051.70", 0x80000, 0xad62cbd4},
     {"epr-13063.85", 0x80000, 0xc580bf6d}, {"epr-13071.113", 0x80000, 0xdf99ef99},
     {"epr-13059.81", 0x80000, 0x4c982558}, {"epr-13067.109", 0x80000, 0xf97f6119},
     {"epr-13046.65", 0x80000, 0xc75a86e9}, {"epr-13054.73", 0x80000, 0x2934549a},
     {"epr-13042.61", 0x80000, 0x53ed97af}, {"epr-13050.69", 0x80000, 0x04429068},
     {"epr-13062.84", 0x80000, 0x4fdb4ee3}, {"epr-13070.112", 0x80000, 0x52ea130e},
     {"epr-13058.80", 0x80000, 0x19ff1626}, {"epr-13066.108", 0x80000, 0xbc70a250},
     {"epr-13045.64", 0x80000, 0x54d5bc6d}, {"epr-13053.72", 0x80000, 0x9502af13},
     {"epr-13041.60", 0x80000, 0xd0a7402c}, {"epr-13049.68", 0x80000, 0x5b9c0b6c},
     {"epr-13061.83", 0x80000, 0x7b95ec3b}, {"epr-13069.111", 0x80000, 0xe1f538f0},
     {"epr-13057.79", 0x80000, 0x73baefee}, {"epr-13065.107", 0x80000, 0x8937a655}},
    {"epr-13033.102", 0x10000, 0x6df5e827},
    {{"epr-13036.107", 0x80000, 0x7890c26c},
     {"epr-13035.106", 0x80000, 0x009fa13e},
     {"epr-13034.105", 0x80000, 0xcd22d95d}},
};
// Strike Fighter: G-LOC's Y sprite ROMs and samples, its own programs
const SetFiles kStrkfgtr = {
    {{"epr-13889.25", 0x20000, 0xf4173114},
     {"epr-13888.24", 0x20000, 0x65e2b226},
     {"epr-13826.27", 0x20000, 0x3d34ea55},
     {"epr-13825.26", 0x20000, 0xfe218d83}},
    2,
    {{"epr-13830.81", 0x20000, 0xf9adc9d1}, {"epr-13829.80", 0x20000, 0xc5cd85dd}},
    {{"epr-13828.54", 0x20000, 0x2470cf5f}, {"epr-13827.53", 0x20000, 0xa9d0cf7d}},
    {{"epr-13833.16", 0x80000, 0x6148e11a},
     {"epr-13832.14", 0x80000, 0x41679754},
     {"epr-13040.17", 0x80000, 0x4aeb3a85},
     {"epr-13038.15", 0x80000, 0x0b2edb6d}},
    {{"epr-13048.67", 0x80000, 0xfe1eb0dd}, {"epr-13056.75", 0x80000, 0x5904f8e6},
     {"epr-13044.63", 0x80000, 0x4d931f89}, {"epr-13052.71", 0x80000, 0x0291f040},
     {"epr-13064.86", 0x80000, 0x5f8e651b}, {"epr-13072.114", 0x80000, 0x6b85641a},
     {"epr-13060.82", 0x80000, 0xee16ad97}, {"epr-13068.110", 0x80000, 0x64d52bbb},
     {"epr-13047.66", 0x80000, 0x53340832}, {"epr-13055.74", 0x80000, 0x39b6b665},
     {"epr-13043.62", 0x80000, 0x208f16fd}, {"epr-13051.70", 0x80000, 0xad62cbd4},
     {"epr-13063.85", 0x80000, 0xc580bf6d}, {"epr-13071.113", 0x80000, 0xdf99ef99},
     {"epr-13059.81", 0x80000, 0x4c982558}, {"epr-13067.109", 0x80000, 0xf97f6119},
     {"epr-13046.65", 0x80000, 0xc75a86e9}, {"epr-13054.73", 0x80000, 0x2934549a},
     {"epr-13042.61", 0x80000, 0x53ed97af}, {"epr-13050.69", 0x80000, 0x04429068},
     {"epr-13062.84", 0x80000, 0x4fdb4ee3}, {"epr-13070.112", 0x80000, 0x52ea130e},
     {"epr-13058.80", 0x80000, 0x19ff1626}, {"epr-13066.108", 0x80000, 0xbc70a250},
     {"epr-13045.64", 0x80000, 0x54d5bc6d}, {"epr-13053.72", 0x80000, 0x9502af13},
     {"epr-13041.60", 0x80000, 0xd0a7402c}, {"epr-13049.68", 0x80000, 0x5b9c0b6c},
     {"epr-13061.83", 0x80000, 0x7b95ec3b}, {"epr-13069.111", 0x80000, 0xe1f538f0},
     {"epr-13057.79", 0x80000, 0x73baefee}, {"epr-13065.107", 0x80000, 0x8937a655}},
    {"epr-13831.102", 0x10000, 0xdabbcea1},
    {{"mpr-13036.107", 0x80000, 0x7890c26c},
     {"mpr-13035.106", 0x80000, 0x009fa13e},
     {"mpr-13034.105", 0x80000, 0xcd22d95d}},
};

// Rail Chase
const SetFiles kRchase = {
    {{"epr-13986.25", 0x20000, 0x388b2365},
     {"epr-13985.24", 0x20000, 0x14dba5d4},
     {"epr-13988.27", 0x20000, 0xdc1cd5a4},
     {"epr-13987.26", 0x20000, 0x43be9e60}},
    2,
    {{"epr-13992a.81", 0x20000, 0xc5d525b6}, {"epr-13991a.80", 0x20000, 0x299e3c7c}},
    {{"epr-14092.54", 0x20000, 0x18eb23c5}, {"epr-14091.53", 0x20000, 0x72a56f71}},
    {{"mpr-13999.16", 0x40000, 0x9a1dd53c}, {"mpr-13997.14", 0x40000, 0x1fdf1b87}},
    {{"mpr-14021.67", 0x80000, 0x9fa88781}, {"mpr-14022.75", 0x80000, 0x49e824bb},
     {"mpr-14009.63", 0x80000, 0x35b5187e}, {"mpr-14010.71", 0x80000, 0x9a538b9b},
     {"mpr-14023.86", 0x80000, 0xe11c6c67}, {"mpr-14024.114", 0x80000, 0x16344535},
     {"mpr-14011.82", 0x80000, 0x78e9983b}, {"mpr-14012.110", 0x80000, 0xe9daa1a4},
     {"mpr-14017.66", 0x80000, 0xb83df159}, {"mpr-14018.74", 0x80000, 0x76dbe9ce},
     {"mpr-14005.62", 0x80000, 0x9e998209}, {"mpr-14006.70", 0x80000, 0x2caddf1a},
     {"mpr-14019.85", 0x80000, 0xb15e19ff}, {"mpr-14020.113", 0x80000, 0x84c7008f},
     {"mpr-14007.81", 0x80000, 0xc3cf5faa}, {"mpr-14008.109", 0x80000, 0x7e91beb2},
     {"mpr-14013.65", 0x80000, 0x31dbb2c3}, {"mpr-14014.73", 0x80000, 0x7e68257d},
     {"mpr-14001.61", 0x80000, 0x71031ad0}, {"mpr-14002.69", 0x80000, 0x27e70a5e},
     {"mpr-14015.84", 0x80000, 0x7540bf85}, {"mpr-14016.112", 0x80000, 0x7d87b94d},
     {"mpr-14003.80", 0x80000, 0x87725d74}, {"mpr-14004.108", 0x80000, 0x73477291}},
    {"epr-13993.102", 0x10000, 0x7cc3b543},
    {{"mpr-13996.107", 0x80000, 0x345f5a41},
     {"mpr-13995.106", 0x80000, 0xf604c270},
     {"mpr-13994.105", 0x80000, 0x76095538}},
};

// ROM_LOAD64_BYTE: groups of eight byte lanes, each group 8 * size bytes
std::vector<uint8_t> load64_byte(const rt::RomSource &src, const File *files, int groups, size_t region) {
    std::vector<uint8_t> out(region, 0);
    for (int g = 0; g < groups; ++g)
        for (int lane = 0; lane < 8; ++lane) {
            const File &f = files[g * 8 + lane];
            const auto d = src.get(f);
            const size_t base = size_t(g) * 8 * f.size;
            for (size_t i = 0; i < d.size(); ++i) out[base + i * 8 + size_t(lane)] = d[i];
        }
    return out;
}

} // namespace

Roms import_rom_set(const std::string &path, Game game) {
    const rt::RomSource src(path);
    const SetFiles &s = game == Game::pdrift     ? kPdrift
                        : game == Game::gloc     ? kGloc
                        : game == Game::strkfgtr ? kStrkfgtr
                        : game == Game::rchase   ? kRchase
                                                 : kGforce2;
    Roms r;
    r.game = game;
    r.main = src.load16_byte(s.main, s.main_pairs, 0x80000);
    r.subx = src.load16_byte(s.subx, 1, 0x40000);
    r.suby = src.load16_byte(s.suby, 1, 0x40000);
    // the sprite regions are as large as their files (MAME's regions:
    // bsprites 0x80000 or 0x200000, ysprites 0x400000, 0xc00000 or 0x1000000)
    int bfiles = 0, yfiles = 0;
    size_t bregion = 0, yregion = 0;
    for (const File &f : s.bsprites)
        if (f.name) ++bfiles, bregion += f.size;
    for (const File &f : s.ysprites)
        if (f.name) ++yfiles, yregion += f.size;
    r.video.bsprites = src.load16_byte(s.bsprites, bfiles / 2, bregion);
    r.video.ysprites = load64_byte(src, s.ysprites, yfiles / 8, yregion);
    r.sound = src.get(s.sound);
    // ROMREGION_ERASEFF; the 0x20000 ROMs ROM_RELOADed through 0x80000
    r.pcm.assign(0x200000, 0xff);
    {
        const auto d = src.get(s.pcm[0]);
        std::copy(d.begin(), d.end(), r.pcm.begin());
    }
    for (int i = 1; i < 3; ++i) {
        const auto d = src.get(s.pcm[i]);
        for (size_t at = 0; at < 0x80000; at += d.size()) std::copy(d.begin(), d.end(), r.pcm.begin() + long(i * 0x80000 + at));
    }
    return r;
}

} // namespace ybd
