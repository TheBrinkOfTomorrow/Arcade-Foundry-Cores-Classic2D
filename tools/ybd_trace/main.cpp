// Runs the native Y Board headless and writes the same trace MAME's
// ORTRACE_LOG does (patches/mame), for lockstep comparison:
//   s<cpu> <insn> <cycles> <pc> <sr> d0-d7 a0-a6 sp othersp   (0 main, 1 X, 2 Y)
//   i<cpu> <insn> <cycles> <level>
//   f <update> <main insns> <X insns> <Y insns> <Z80 cycles> <index hash> <RGB hash>
//
//   ybd_trace ROMSET FRAMES [--every N] [--dense FROM:TO] [--replay FILE]
//
// ROMSET is the MAME set (gforce2.zip, pdrift.zip, gloc.zip, strkfgtr.zip or rchase.zip,
// or a folder of its files); YBD_SET=<set> picks the game (gforce2 by default).
#include "yboard/replay.h"
#include "yboard/rom_import.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

uint64_t g_mask = 4095, g_dense_from = 0, g_dense_to = 0;
const char *g_fbdir = nullptr; // ORUN_FBDUMP_DIR: f<update>.rgb (320x224 RGB) every ORUN_FBDUMP_EVERY
uint64_t g_fbevery = 1;

struct Tracer {
    int id;
    orun::MusashiExec *cpu;
    bool snap = false;

    uint64_t next_after(uint64_t n) const {
        uint64_t nx = (n | g_mask) + 1;
        if (n + 1 < g_dense_to && n + 1 >= g_dense_from) nx = n + 1;
        else if (n + 1 < g_dense_from && g_dense_from < nx) nx = g_dense_from;
        return nx;
    }
    void hook(uint32_t pc) {
        m68k::Cpu &c = cpu->c;
        std::printf("s%d %llu %llu %06x %04x", id, (unsigned long long)c.insns, (unsigned long long)c.cyc, pc, c.sr());
        for (int i = 0; i < 8; ++i) std::printf(" %08x", c.d[i]);
        for (int i = 0; i < 7; ++i) std::printf(" %08x", c.a[i]);
        std::printf(" %08x %08x\n", c.a[7], c.other_sp);
        cpu->e.hook_at = next_after(c.insns);
    }
    void attach() {
        cpu->e.hook_at = 0;
        cpu->e.on_hook = [this](uint32_t pc) { hook(pc); };
        cpu->on_interrupt = [this](int level, uint64_t at) {
            std::printf("i%d %llu %llu %d\n", id, (unsigned long long)cpu->c.insns, (unsigned long long)at, level);
            cpu->e.hook_at = cpu->c.insns;
        };
    }
};

} // namespace

int main(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: ybd_trace ROMSET FRAMES [--every N] [--dense FROM:TO] [--replay FILE]\n");
        return 2;
    }
    ybd::Roms roms;
    try {
        ybd::Game g = ybd::Game::gforce2;
        if (const char *set = std::getenv("YBD_SET"))
            if (!std::strcmp(set, ybd::kPdriftRomSet)) g = ybd::Game::pdrift;
            else if (!std::strcmp(set, ybd::kGlocRomSet)) g = ybd::Game::gloc;
            else if (!std::strcmp(set, ybd::kStrkfgtrRomSet)) g = ybd::Game::strkfgtr;
            else if (!std::strcmp(set, ybd::kRchaseRomSet)) g = ybd::Game::rchase;
        roms = ybd::import_rom_set(argv[1], g);
    } catch (const std::exception &e) {
        std::fprintf(stderr, "ybd_trace: %s\n", e.what());
        return 2;
    }
    const long frames = std::strtol(argv[2], nullptr, 10);
    ybd::Replay replay;
    for (int i = 3; i < argc; ++i)
        if (!std::strcmp(argv[i], "--replay") && i + 1 < argc) {
            if (!replay.load(argv[++i])) {
                std::fprintf(stderr, "ybd_trace: cannot read %s\n", argv[i]);
                return 2;
            }
        } else if (!std::strcmp(argv[i], "--every") && i + 1 < argc) g_mask = std::strtoull(argv[++i], nullptr, 10) - 1;
        else if (!std::strcmp(argv[i], "--dense") && i + 1 < argc) {
            const char *s = argv[++i];
            g_dense_from = std::strtoull(s, nullptr, 10);
            if (const char *c = std::strchr(s, ':')) g_dense_to = std::strtoull(c + 1, nullptr, 10);
        }

    g_fbdir = std::getenv("ORUN_FBDUMP_DIR");
    if (const char *e = std::getenv("ORUN_FBDUMP_EVERY")) g_fbevery = std::max<uint64_t>(1, std::strtoull(e, nullptr, 10));
    ybd::Board b(std::move(roms));
    Tracer tm{0, &b.main()}, tx{1, &b.subx()}, ty{2, &b.suby()};
    tm.attach();
    tx.attach();
    ty.attach();
    uint64_t frame = 0;
    b.on_screen_update = [&](int top, int bottom) {
        // MAME counts an instruction when it starts
        auto insns = [](orun::MusashiExec &c) { return c.c.insns + (c.executing() ? 1 : 0); };
        uint64_t hi = 1469598103934665603ULL, hr = 1469598103934665603ULL;
        ybd::Video &v = b.video();
        for (size_t i = 0; i < v.bitmap.size(); ++i) {
            const int y = int(i / 320);
            if (y < top || y > bottom) continue;
            const uint16_t px = v.bitmap[i];
            hi = (hi ^ px) * 1099511628211ULL;
            hr = (hr ^ v.rgb(px)) * 1099511628211ULL;
        }
        if (g_fbdir && !(frame % g_fbevery)) {
            char path[1024];
            std::snprintf(path, sizeof path, "%s/f%06llu.rgb", g_fbdir, (unsigned long long)frame);
            if (FILE *f = std::fopen(path, "wb")) {
                for (uint16_t px : v.bitmap) {
                    const uint32_t c = v.rgb(px);
                    const uint8_t rgb[3] = {uint8_t(c >> 16), uint8_t(c >> 8), uint8_t(c)};
                    std::fwrite(rgb, 1, 3, f);
                }
                std::fclose(f);
            }
        }
        std::printf("f %llu %llu %llu %llu %llu %016llx %016llx\n", (unsigned long long)frame, (unsigned long long)insns(b.main()),
                    (unsigned long long)insns(b.subx()), (unsigned long long)insns(b.suby()), (unsigned long long)b.sound().cpu().total_cycles(),
                    (unsigned long long)hi, (unsigned long long)hr);
        if (frame) replay.apply(frame - 1, b.in); // MAME applies UPDATE at the end of update UPDATE+1
        ++frame;
    };
    try {
        while (long(b.frame()) < frames) b.run_frame();
    } catch (const std::exception &ex) {
        std::fflush(stdout);
        std::fprintf(stderr, "ybd_trace: %s (update %llu)\n", ex.what(), (unsigned long long)frame);
        return 1;
    }
    return 0;
}
