// Runs the native System 32 / Multi 32 board headless and writes the lines the
// MAME oracle (patches/mame/0003) writes, for lockstep comparison:
//   M1TRACE_PCLOG:  <insn> <pc> <psw> <hash of r0-r31>   every --every insns
//   M1TRACE_FRAMES: f <frame> <V70 insns> <Z80 cycles> <minx>-<maxx> <hash>
//
//   s32_trace ROMSET FRAMES [--every N] [--dense FROM:TO] [--pclog FILE] [--frames FILE] [--replay FILE]
//
// --replay: "VBL PORT VALUE" lines, as MAME's M1TRACE_REPLAY (patch 0003):
// from start_of_vblank_int number VBL the port (P1_A ... SERVICE34_B,
// ANALOG1-8) reads VALUE.
//
// ROMSET is the MAME set (orunners.zip, radr.zip: the set is the file's
// name). Link with the recompiled code (v60recomp output for the set).
#include "system32/board.h"
#include "system32/rom_import.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace v60n {
void run_native(v60::Core &c, NativeEnv &env);
}

namespace {

uint64_t g_mask = 4095, g_from = 0, g_to = 0;
FILE *g_pc = stdout;
s32::V60Exec *g_exec = nullptr;

struct ReplayEv {
    uint64_t vbl;
    int port;
    uint8_t value;
};
std::vector<ReplayEv> g_replay;

bool load_replay(const char *path) {
    FILE *f = std::fopen(path, "r");
    if (!f) return false;
    static const char *const names[18] = {"P1_A", "P2_A", "PORTC_A", "SERVICE12_A", "SERVICE34_A", "P1_B", "P2_B", "PORTC_B", "SERVICE12_B",
                                          "SERVICE34_B", "ANALOG1", "ANALOG2", "ANALOG3", "ANALOG4", "ANALOG5", "ANALOG6", "ANALOG7", "ANALOG8"};
    char line[256];
    while (std::fgets(line, sizeof line, f)) {
        unsigned long long u;
        char name[32];
        unsigned v;
        if (line[0] == '#' || std::sscanf(line, "%llu %31s %x", &u, name, &v) != 3) continue;
        for (int i = 0; i < 18; ++i)
            if (!std::strcmp(name, names[i])) g_replay.push_back({u, i, uint8_t(v)});
    }
    std::fclose(f);
    return true;
}

void apply_replay(uint64_t vbl, s32::Inputs &in) {
    static size_t pos = 0;
    uint8_t *const ports[10] = {&in.p1_a, &in.p2_a, &in.portc_a, &in.service12_a, &in.service34_a,
                                &in.p1_b, &in.p2_b, &in.portc_b, &in.service12_b, &in.service34_b};
    for (; pos < g_replay.size() && g_replay[pos].vbl <= vbl; ++pos) {
        const ReplayEv &e = g_replay[pos];
        if (e.port < 10) *ports[e.port] = e.value;
        else in.analog[e.port - 10] = e.value;
    }
}

bool wanted(uint64_t n) { return !(n & g_mask) || (n >= g_from && n < g_to); }
uint64_t next_wanted(uint64_t n) {
    if (n >= g_from && n < g_to) return n;
    uint64_t nx = (n + g_mask) & ~g_mask;
    if (n < g_from && g_from < nx) nx = g_from;
    return nx;
}

uint64_t reg_hash(const v60::Core &c) {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (int i = 0; i < 32; i++) h = (h ^ c.m_reg[i]) * 0x100000001b3ULL;
    return h;
}

// run_native, stopping before each traced instruction to note its address
// and after it to print the state MAME prints
void traced_run(v60::Core &c, v60n::NativeEnv &e) {
    uint64_t lim = e.limit;
    // a device access can cut the slice short (set_live_icount moves
    // env.limit): keep the new limit
    auto run_to = [&](uint64_t to) {
        const uint64_t cuts = g_exec->cuts;
        e.limit = to;
        v60n::run_native(c, e);
        if (g_exec->cuts != cuts) lim = e.limit;
    };
    while (e.count < lim) {
        const uint64_t n = next_wanted(e.count);
        if (n >= lim) {
            run_to(lim);
            break;
        }
        if (e.count < n) {
            run_to(n);
            if (e.count < n) break; // stopped for an interrupt or the slice was cut
        }
        if (e.count >= lim) break;
        const uint32_t pc = c.pc();
        run_to(n + 1);
        if (e.count == n + 1)
            std::fprintf(g_pc, "%llu %08x %08x %016llx\n", (unsigned long long)n, pc, c.psw(), (unsigned long long)reg_hash(c));
        else
            break;
    }
    e.limit = lim;
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: s32_trace ROMSET FRAMES [--every N] [--dense FROM:TO] [--pclog FILE] [--frames FILE] [--replay FILE]\n");
        return 2;
    }
    const uint64_t frames = std::strtoull(argv[2], nullptr, 10);
    FILE *ff = stdout;
    for (int i = 3; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--every") && i + 1 < argc) g_mask = std::strtoull(argv[++i], nullptr, 10) - 1;
        else if (!std::strcmp(argv[i], "--dense") && i + 1 < argc) {
            const char *s = argv[++i];
            g_from = std::strtoull(s, nullptr, 10);
            if (const char *c = std::strchr(s, ':')) g_to = std::strtoull(c + 1, nullptr, 10);
        } else if (!std::strcmp(argv[i], "--pclog") && i + 1 < argc) g_pc = std::fopen(argv[++i], "w");
        else if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) ff = std::fopen(argv[++i], "w");
        else if (!std::strcmp(argv[i], "--replay") && i + 1 < argc) {
            if (!load_replay(argv[++i])) {
                std::fprintf(stderr, "s32_trace: cannot read %s\n", argv[i]);
                return 2;
            }
        }
    }
    if (!g_pc || !ff) {
        std::fprintf(stderr, "s32_trace: cannot open an output file\n");
        return 2;
    }
    std::string set = argv[1];
    if (const size_t slash = set.find_last_of("/\\"); slash != std::string::npos) set = set.substr(slash + 1);
    if (const size_t dot = set.find('.'); dot != std::string::npos) set = set.substr(0, dot);
    s32::Board b(s32::import_rom_set(argv[1], set == s32::kRadrRomSet ? s32::kRadrRomSet : s32::kOrunnersRomSet), traced_run);
    g_exec = &b.main();
    uint64_t frame = 0;
    if (std::getenv("S32_ACC")) b.on_access = [&](bool w, uint32_t a, uint32_t v, uint32_t m) {
        if ((a & 0xff0000) == 0xd00000) std::fprintf(g_pc, "A %c %llu %06x %08x %08x lim=%llu\n", w ? 'W' : 'R', (unsigned long long)b.main().env.count, a, v, m, (unsigned long long)b.main().env.limit);
    };
    b.main().on_interrupt = [&](int vector, uint32_t pc, uint64_t at) {
        std::fprintf(g_pc, "i %llu %d %08x\n", (unsigned long long)at, vector, pc);
    };
    uint64_t vbl = 0;
    b.on_frame = [&] {
        // on_frame comes just before start_of_vblank_int, where MAME applies
        // the replay
        apply_replay(vbl++, b.inputs);
        const int w = b.video().width();
        uint64_t h = 1469598103934665603ULL;
        for (int y = 0; y < 224; ++y)
            for (int x = 0; x < w; ++x) h = (h ^ (b.screen[0][size_t(y) * 416 + size_t(x)] & 0xffffff)) * 1099511628211ULL;
        std::fprintf(ff, "f %llu %llu %llu 0-%d %016llx\n", (unsigned long long)frame++, (unsigned long long)b.main().env.count,
                     (unsigned long long)b.soundcpu().total_cycles(), w - 1, (unsigned long long)h);
        // S32_DUMP=DIR writes every frame as DIR/f<frame>.ppm
        if (const char *d = std::getenv("S32_DUMP")) {
            char name[512];
            std::snprintf(name, sizeof(name), "%s/f%llu.ppm", d, (unsigned long long)(frame - 1));
            if (FILE *o = std::fopen(name, "wb")) {
                std::fprintf(o, "P6 %d 224 255\n", w);
                for (int y = 0; y < 224; ++y)
                    for (int x = 0; x < w; ++x) {
                        const uint32_t c = b.screen[0][size_t(y) * 416 + size_t(x)];
                        std::fputc((c >> 16) & 0xff, o); std::fputc((c >> 8) & 0xff, o); std::fputc(c & 0xff, o);
                    }
                std::fclose(o);
            }
        }
    };
    try {
        while (frame < frames) b.run_frame();
    } catch (const std::exception &ex) {
        std::fflush(g_pc);
        std::fflush(ff);
        std::fprintf(stderr, "s32_trace: %s\n", ex.what());
        return 1;
    }
    return 0;
}
