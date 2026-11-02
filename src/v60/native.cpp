#include "v60/native.h"

#include <cstdio>

namespace v60n {

void NativeEnv::miss(v60::Core &c) {
    char b[96];
    std::snprintf(b, sizeof b, "V60: no recompiled code at %06x (add it to the seeds)", c.m_reg[32] & 0xffffff);
    throw v60::Fatal(b);
}

} // namespace v60n
