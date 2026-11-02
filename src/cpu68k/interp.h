// The fallback 68000 interpreter: runs code the recompiler did not reach.
//
// The recompiled program (tools/m68krecomp) only holds the code that MAME
// runs, jump tables and handler pointers led it to. When control reaches
// any other address the generated dispatch calls Env::missing, and the
// board hands it to this interpreter: it decodes each instruction with the
// recompiler's own decoder (src/m68k/decode) and runs it with the same flag
// helpers, cycle counts, slice-end handling and interrupt sampling as the
// generated code (src/m68k/timing.h), until control is back at an address
// that has recompiled code, the slice ends, or the instruction is one the
// board must look at (status register changes, exceptions, STOP).
//
// Opcode words come from the CPU's memory map, or, for an FD1094 program,
// from the decrypted opcode image the recompiler read (set_opcodes).
#pragma once

#include "cpu68k/gen_support.h"
#include "m68k/decode.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace m68k {

class Interp {
public:
    using HasCode = bool (*)(uint32_t);

    // Instruction fetches below image.size() read the image (big-endian);
    // the rest go through the memory map.
    void set_opcodes(std::vector<uint8_t> image) {
        op_ = std::move(image);
        cache_.clear();
    }

    // Runs from c.pc until has_code(c.pc) (null: never), the slice's end
    // or an instruction that returns to the board.
    void run(Env &e, HasCode has_code);

    uint64_t instructions = 0; // run here, for the core's statistics
    uint32_t last_entry = 0;   // the address the last fallback started at

private:
    enum class Next { Fall, Jump, Return };
    bool fetch(Cpu &c, uint32_t addr, uint16_t &w) const;
    const Insn *decode_at(Cpu &c, uint32_t pc, Insn &tmp);
    // one instruction; false: return to the board (c.pc set)
    bool step(Env &e, const Insn &in);
    Next exec(Env &e, const Insn &in, uint32_t &target);

    std::vector<uint8_t> op_;
    std::unordered_map<uint32_t, Insn> cache_;
};

} // namespace m68k
