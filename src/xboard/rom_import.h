// Loads the user's After Burner II or Thunder Blade ROM set (MAME's
// aburner2, thndrbld or thndrbld1: a .zip, or a folder of its files) into the X Board's images, every file CRC-checked
// against MAME's ROM_START and laid out as MAME loads it.
#pragma once

#include "runtime/rom_source.h"
#include "xboard/board.h"

#include <string>

namespace xbd {

using RomError = rt::RomError;

// The MAME set the recompiled code was made from.
inline constexpr const char *kRomSet = "aburner2";
inline constexpr const char *kThndrbldRomSet = "thndrbld";
inline constexpr const char *kThndrbld1RomSet = "thndrbld1";

// Throws RomError naming the first missing or bad file.
Roms import_rom_set(const std::string &path, Game game = Game::aburner2);

} // namespace xbd
