// Loads the user's Hang-On board ROM set (MAME's hangon, sharrier or enduror: a
// .zip, or a folder of its files) into the board's images, every file
// CRC-checked against MAME's ROM_START and laid out as MAME loads it.
#pragma once

#include "hangon/board.h"
#include "runtime/rom_source.h"

#include <string>

namespace hang {

using RomError = rt::RomError;

inline constexpr const char *kRomSet = "hangon";
inline constexpr const char *kSharrierRomSet = "sharrier";
inline constexpr const char *kEndurorRomSet = "enduror";

// Throws RomError naming the first missing or bad file.
Roms import_rom_set(const std::string &path, Game game = Game::hangon);

} // namespace hang
