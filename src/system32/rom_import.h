// Loads the user's OutRunners or Rad Rally ROM set (MAME's orunners, radr:
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

// set: kOrunnersRomSet or kRadrRomSet. Throws RomError naming the first
// missing or bad file.
Roms import_rom_set(const std::string &path, const std::string &set = kOrunnersRomSet);

} // namespace s32
