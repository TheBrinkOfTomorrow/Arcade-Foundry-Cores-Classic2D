// Loads the user's Out Run, Super Hang-On or Turbo Out Run ROM set (MAME's
// outrun, shangon or toutrun: a .zip, or a folder of its files) into the board's images, every file CRC-checked against
// MAME's ROM_START and laid out as MAME loads it (ROM_LOAD16_BYTE program
// interleave, ROM_LOAD32_BYTE sprite lanes, the PCM ROMs' ROM_RELOADs).
#pragma once

#include "outrun/board.h"
#include "runtime/rom_source.h"

#include <string>

namespace orun {

using RomError = rt::RomError;

// The MAME sets the recompiled code is made from.
inline constexpr const char *kRomSet = "outrun";
inline constexpr const char *kShangonRomSet = "shangon";
inline constexpr const char *kToutrunRomSet = "toutrun";

// Throws RomError naming the first missing or bad file.
Roms import_rom_set(const std::string &path, Game game = Game::outrun);

} // namespace orun
