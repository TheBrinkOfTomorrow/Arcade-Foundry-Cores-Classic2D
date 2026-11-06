// Loads the user's OutRunners, Rad Rally, Rad Mobile, F1 Exhaust Note, Jurassic Park, SegaSonic or Golden Axe: The
// Revenge of Death Adder ROM set (MAME's orunners, radr, radm, f1en, jpark, sonic, ga2:
// a .zip, or a folder of its files) into the board's images, every file
// CRC-checked against MAME's ROM_START and laid out as MAME loads it.
#pragma once

#include "runtime/rom_source.h"
#include "system32/board.h"

#include <string>

namespace s32 {

using RomError = rt::RomError;

inline constexpr const char *kOrunnersRomSet = "orunners";
inline constexpr const char *kRadrRomSet = "radr";
inline constexpr const char *kRadmRomSet = "radm";
inline constexpr const char *kF1enRomSet = "f1en"; // two boards, the same ROMs
inline constexpr const char *kJparkRomSet = "jpark";
inline constexpr const char *kSonicRomSet = "sonic"; // the trackball board
inline constexpr const char *kGa2RomSet = "ga2";     // the V25 protection board

// set: kOrunnersRomSet, kRadrRomSet or kRadmRomSet. Throws RomError naming the first
// missing or bad file.
Roms import_rom_set(const std::string &path, const std::string &set = kOrunnersRomSet);

} // namespace s32
