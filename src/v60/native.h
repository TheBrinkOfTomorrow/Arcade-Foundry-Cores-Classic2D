// What recompiled V60/V70 code (tools/v60recomp output) runs against:
// v60::Core's handlers (MAME's semantics) with per-instruction hooks, and
// the stop condition the caller sets. From Arcade-Foundry-Cores-Model12's
// src/model1/native.h (ours), made board-neutral.
#pragma once

#include "v60/core.h"

#include <cstdint>
#include <string>

namespace v60n {

// Base of the generated per-instruction hook classes: a hook the generator
// did not emit is never called (the handler's decoding is fixed per
// instruction), so these only document the interface.
struct NativeHooks {
    static void enter(v60::Core &, const char *) {}
};

struct NativeEnv {
    uint64_t count = 0;          // instructions completed
    uint64_t limit = UINT64_MAX; // run_native returns before instruction `limit`
    // An interrupt is pending: run_native also returns after an instruction
    // that enables interrupts (UPDPSW, LDPR, RETIU, RETIS)
    bool irq_pending = false;
    // Control reached an address with no recompiled code: a hard error
    // (throws v60::Fatal naming it).
    virtual void miss(v60::Core &c);
    virtual ~NativeEnv() = default;
};

// Runs recompiled code from c's PC until env.count reaches env.limit (PC is
// then the next instruction's) or control reaches an address with no code.
void run_native(v60::Core &c, NativeEnv &env);
// How many instructions were recompiled.
uint64_t native_instructions();

} // namespace v60n
