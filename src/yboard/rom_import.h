// Loads the user's Galaxy Force 2, Power Drift, G-LOC or Strike Fighter ROM
// set (MAME's gforce2, pdrift, gloc or strkfgtr: a .zip, or a folder of its files) into the Y Board's images,
// every file CRC-checked against MAME's ROM_START and laid out as MAME
// loads it.
#pragma once

#include "runtime/rom_source.h"
#include "yboard/board.h"

#include <string>

namespace ybd {

using RomError = rt::RomError;

inline constexpr const char *kGforce2RomSet = "gforce2";
inline constexpr const char *kPdriftRomSet = "pdrift";
inline constexpr const char *kGlocRomSet = "gloc";
inline constexpr const char *kStrkfgtrRomSet = "strkfgtr";
inline constexpr const char *kRchaseRomSet = "rchase";

// Throws RomError naming the first missing or bad file.
Roms import_rom_set(const std::string &path, Game game);

} // namespace ybd
