// The FD1094 program as the recompiler sees it: each word of the main ROM
// decrypted in the state the CPU fetches it in. MAME's runs give the states
// (ORTRACE_PCS writes "PC STATE" for an FD1094); every instruction at a
// listed PC takes that PC's state, the rest of the ROM the state most PCs
// ran in, and words 0-3 are the reset vector fetch (state 0).
#pragma once

#include "fd1094/fd1094.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace fd1094 {

struct ImageReport {
    size_t listed = 0, conflicts = 0;
    uint8_t default_state = 0;
    std::vector<std::pair<uint8_t, size_t>> states; // state, PCs
};

// raw: the main ROM (big-endian words); pcs: (PC, state) from the seeds.
std::vector<uint8_t> opcode_image(const std::vector<uint8_t> &raw, const Fd1094 &fd, const std::vector<std::pair<uint32_t, uint8_t>> &pcs,
                                  ImageReport *report = nullptr);

// Reads "PC STATE" lines (hex; lines without a state are skipped).
std::vector<std::pair<uint32_t, uint8_t>> read_state_seeds(const std::string &path);

} // namespace fd1094
