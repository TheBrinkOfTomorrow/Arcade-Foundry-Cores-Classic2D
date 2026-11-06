// license:BSD-3-Clause
// copyright-holders:Nicola Salmoria, Andreas Naive, Charles MacDonald
//
// Hitachi FD1089A/FD1089B (MAME's fd1089.cpp): the encrypted 68000. The
// whole program is decrypted up front, once for instruction fetches
// (opcodes, the reset vectors) and once for data reads; only 8 of each
// word's 16 bits are encrypted, with the 0x2000-byte key (0x1000 opcode
// keys, then 0x1000 data keys, picked by address bits ff022a).
#pragma once

#include <cstdint>
#include <vector>

namespace fd1089 {

enum class Variant { a, b };

uint16_t decrypt_one(uint32_t addr, uint16_t val, const uint8_t *key, bool opcode, Variant v);

// image: the CPU's region as big-endian words; fills opcodes and data
// (both the image's size) as big-endian words
void decrypt(const std::vector<uint8_t> &image, const std::vector<uint8_t> &key, Variant v, std::vector<uint8_t> &opcodes,
             std::vector<uint8_t> &data);

} // namespace fd1089
