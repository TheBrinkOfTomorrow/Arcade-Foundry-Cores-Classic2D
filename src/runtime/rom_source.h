// A MAME ROM set as the user has it (a .zip, or a folder of its files):
// each file is found by its MAME name (without case) or, in a zip, by its
// CRC, and checked against the size and CRC in MAME's ROM_START.
#pragma once

#include "afcommon/zip.h"

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace rt {

struct RomError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct RomFile {
    const char *name;
    uint32_t size, crc;
};

class RomSource {
public:
    explicit RomSource(const std::string &path); // throws RomError
    std::vector<uint8_t> get(const RomFile &f) const; // throws RomError naming the file

    // ROM_LOAD16_BYTE pairs (even, odd), each pair 2 * size bytes, from 0
    std::vector<uint8_t> load16_byte(const RomFile *files, int pairs, size_t region) const;
    // ROM_LOAD32_BYTE groups of four lanes, each group 4 * size bytes, from 0
    std::vector<uint8_t> load32_byte(const RomFile *files, int groups, size_t region) const;

private:
    std::string dir_;
    std::unique_ptr<afc::Zip> zip_;
};

} // namespace rt
