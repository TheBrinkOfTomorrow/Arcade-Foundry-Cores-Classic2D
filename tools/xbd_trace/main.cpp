// Runs the native X Board headless and writes the same trace MAME's
// ORTRACE_LOG does (patches/mame), for lockstep comparison:
//   s<cpu> <insn> <cycles> <pc> <sr> d0-d7 a0-a6 sp othersp
//   i<cpu> <insn> <cycles> <level>
//   s2 <insn> <cycles> <pc> <af> <bc> <de> <hl> <ix> <iy> <sp>   (Z80)
//   f <frame> <main insns> <sub insns> <Z80 cycles>
//
//   xbd_trace ROMSET FRAMES [--every N] [--dense FROM:TO] [--zevery N] [--zdense FROM:TO] [--replay FILE]
//
// ROMSET is the MAME set (aburner2.zip or a folder of its files).
#include "xboard/replay.h"
#include "xboard/rom_import.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

namespace {

uint64_t g_mask = 4095, g_dense_from = 0, g_dense_to = 0;
const char *g_fbdir = nullptr; // ORUN_FBDUMP_DIR: f<frame>.rgb (320x224 RGB) every ORUN_FBDUMP_EVERY
uint64_t g_fbevery = 1;
// ORUN_WATCH=OFFSET:FRAME (hex offset into the sub CPU's RAM): from screen
// update FRAME on, every instruction of either CPU checks the word there and
// prints "w CPU INSNS CYC PC OLD NEW" when it has changed
const uint8_t *g_watch = nullptr;
uint64_t g_watch_from = ~0ull;
bool g_watch_on = false;
uint16_t g_watch_val = 0;
uint64_t g_zmask = 4095, g_zfrom = 0, g_zto = 0, g_zinsn = 0;

struct Tracer {
    int id;
    xbd::M68kExec *cpu;
    bool snap = false;

    uint64_t next_after(uint64_t n) const {
        uint64_t nx = (n | g_mask) + 1;
        if (n + 1 < g_dense_to && n + 1 >= g_dense_from) nx = n + 1;
        else if (n + 1 < g_dense_from && g_dense_from < nx) nx = g_dense_from;
        return nx;
    }
    uint64_t sched_at = 0;
    void hook(uint32_t pc) {
        m68k::Cpu &c = cpu->c;
        if (g_watch_on) {
            const uint16_t v = uint16_t(g_watch[0] << 8 | g_watch[1]);
            if (v != g_watch_val)
                std::printf("w %d %llu %llu %06x %04x %04x\n", id, (unsigned long long)c.insns, (unsigned long long)c.cyc, pc, g_watch_val, v);
            g_watch_val = v;
            if (c.insns < sched_at) {
                cpu->e.hook_at = c.insns + 1;
                return;
            }
        }
        std::printf("s%d %llu %llu %06x %04x", id, (unsigned long long)c.insns, (unsigned long long)c.cyc, pc, c.sr());
        for (int i = 0; i < 8; ++i) std::printf(" %08x", c.d[i]);
        for (int i = 0; i < 7; ++i) std::printf(" %08x", c.a[i]);
        std::printf(" %08x %08x\n", c.a[7], c.other_sp);
        sched_at = next_after(c.insns);
        cpu->e.hook_at = g_watch_on ? c.insns + 1 : sched_at;
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
        std::fprintf(stderr, "usage: xbd_trace ROMSET FRAMES [--every N] [--dense FROM:TO] [--zevery N] [--zdense FROM:TO] [--replay FILE]\n");
        return 2;
    }
    xbd::Roms roms;
    try {
        // XBD_SET=aburner131, thndrbld, thndrbld1, rachero, smgp, loffire or gprider: that set (default aburner2)
        xbd::Game g = xbd::Game::aburner2;
        if (const char *set = std::getenv("XBD_SET")) {
            if (!std::strcmp(set, xbd::kThndrbldRomSet)) g = xbd::Game::thndrbld;
            else if (!std::strcmp(set, xbd::kThndrbld1RomSet)) g = xbd::Game::thndrbld1;
            else if (!std::strcmp(set, xbd::kAburner131RomSet)) g = xbd::Game::aburner131;
            else if (!std::strcmp(set, xbd::kRacheroRomSet)) g = xbd::Game::rachero;
            else if (!std::strcmp(set, xbd::kSmgpRomSet)) g = xbd::Game::smgp;
            else if (!std::strcmp(set, xbd::kLoffireRomSet)) g = xbd::Game::loffire;
            else if (!std::strcmp(set, xbd::kGpriderRomSet)) g = xbd::Game::gprider;
        }
        roms = xbd::import_rom_set(argv[1], g);
    } catch (const std::exception &e) {
        std::fprintf(stderr, "xbd_trace: %s\n", e.what());
        return 2;
    }
    const long frames = std::strtol(argv[2], nullptr, 10);
    xbd::Replay replay;
    for (int i = 3; i < argc; ++i)
        if (!std::strcmp(argv[i], "--replay") && i + 1 < argc) {
            if (!replay.load(argv[++i])) {
                std::fprintf(stderr, "xbd_trace: cannot read %s\n", argv[i]);
                return 2;
            }
        } else if (!std::strcmp(argv[i], "--every") && i + 1 < argc) g_mask = std::strtoull(argv[++i], nullptr, 10) - 1;
        else if (!std::strcmp(argv[i], "--zevery") && i + 1 < argc) g_zmask = std::strtoull(argv[++i], nullptr, 10) - 1;
        else if (!std::strcmp(argv[i], "--zdense") && i + 1 < argc) {
            const char *s = argv[++i];
            g_zfrom = std::strtoull(s, nullptr, 10);
            if (const char *c = std::strchr(s, ':')) g_zto = std::strtoull(c + 1, nullptr, 10);
        } else if (!std::strcmp(argv[i], "--dense") && i + 1 < argc) {
            const char *s = argv[++i];
            g_dense_from = std::strtoull(s, nullptr, 10);
            if (const char *c = std::strchr(s, ':')) g_dense_to = std::strtoull(c + 1, nullptr, 10);
        }

    g_fbdir = std::getenv("ORUN_FBDUMP_DIR");
    if (const char *e = std::getenv("ORUN_FBDUMP_EVERY")) g_fbevery = std::max<uint64_t>(1, std::strtoull(e, nullptr, 10));
    xbd::Board b(std::move(roms));
    if (const char *w = std::getenv("ORUN_WATCH")) {
        { const unsigned long o = std::strtoul(w, nullptr, 16); g_watch = (o & 0x4000 ? &b.subram1[0] : &b.subram0[0]) + (o & 0x3ffe); }
        if (const char *c = std::strchr(w, ':')) g_watch_from = std::strtoull(c + 1, nullptr, 10);
    }
    // ORUN_SCHED=FROM:TO (seconds): the timeslices and timers in that window
    if (const char *w = std::getenv("ORUN_SCHED")) {
        static double from = std::strtod(w, nullptr), to = 1e9;
        if (const char *c = std::strchr(w, ':')) to = std::strtod(c + 1, nullptr);
        b.scheduler().log = [&b](const char *what, const std::string &name, sched::attotime t) {
            const double d = t.as_double();
            if (d < from || d > to) return;
            if (!std::strcmp(what, "target"))
                std::printf("t %.18f main %llu/%llu sub %llu/%llu snd %llu\n", d, (unsigned long long)b.main().c.cyc, (unsigned long long)b.main().c.insns,
                            (unsigned long long)b.sub().c.cyc, (unsigned long long)b.sub().c.insns, (unsigned long long)b.sound().cpu().total_cycles());
            else if (!std::strcmp(what, "ran"))
                std::printf("r :%s\n", name.c_str());
            else
                std::printf("T %s %.18f\n", name.c_str(), d);
        };
    }
    Tracer tm{0, &b.main()}, ts{1, &b.sub()};
    tm.attach();
    ts.attach();
    {
        z80::Z80 &z = b.sound().cpu();
        z.on_insn = [&z](uint16_t pc) {
            if (!(g_zinsn & g_zmask) || (g_zinsn >= g_zfrom && g_zinsn < g_zto))
                std::printf("s2 %llu %llu %04x %04x %04x %04x %04x %04x %04x %04x\n", (unsigned long long)g_zinsn,
                            (unsigned long long)z.total_cycles(), pc, z.af(), z.bc(), z.de(), z.hl(), z.ix(), z.iy(), z.sp());
            ++g_zinsn;
        };
    }
    uint64_t frame = 0;
    // ORTRACE_HASH_CLIP: hash only the rows this update drew, as the patched
    // MAME does (it alternates two screen bitmaps between frames)
    static const bool hash_clip = std::getenv("ORTRACE_HASH_CLIP") != nullptr;
    b.on_screen_update = [&](int top, int bottom) {
        // MAME counts an instruction when it starts
        const uint64_t mi = b.main().c.insns + (b.main().executing() ? 1 : 0), si = b.sub().c.insns + (b.sub().executing() ? 1 : 0);
        uint64_t hi = 1469598103934665603ULL, hr = 1469598103934665603ULL;
        if (orun::Video *v = b.video()) {
            for (size_t i = 0; i < v->bitmap.size(); ++i) {
                const int y = int(i / 320);
                if (hash_clip && (y < top || y > bottom)) continue;
                const uint16_t px = v->bitmap[i];
                hi = (hi ^ px) * 1099511628211ULL;
                hr = (hr ^ v->rgb(px)) * 1099511628211ULL;
            }
            if (g_fbdir && !(frame % g_fbevery)) {
                char path[1024];
                std::snprintf(path, sizeof path, "%s/f%06llu.rgb", g_fbdir, (unsigned long long)frame);
                if (FILE *f = std::fopen(path, "wb")) {
                    for (uint16_t px : v->bitmap) {
                        const uint32_t c = v->rgb(px);
                        const uint8_t rgb[3] = {uint8_t(c >> 16), uint8_t(c >> 8), uint8_t(c)};
                        std::fwrite(rgb, 1, 3, f);
                    }
                    std::fclose(f);
                }
            }
        }
        std::printf("f %llu %llu %llu %llu %016llx %016llx\n", (unsigned long long)frame, (unsigned long long)mi, (unsigned long long)si,
                    (unsigned long long)b.sound().cpu().total_cycles(), (unsigned long long)hi, (unsigned long long)hr);
        if (frame) replay.apply(frame - 1, b.in); // MAME applies UPDATE at the end of update UPDATE+1
        if (frame + 1 == g_watch_from) {
            g_watch_on = true;
            g_watch_val = uint16_t(g_watch[0] << 8 | g_watch[1]);
            tm.cpu->e.hook_at = std::min(tm.cpu->e.hook_at, b.main().c.insns + 1);
            ts.cpu->e.hook_at = std::min(ts.cpu->e.hook_at, b.sub().c.insns + 1);
        }
        ++frame;
    };
    try {
        while (long(b.frame()) < frames) b.run_frame();
    } catch (const std::exception &ex) {
        std::fflush(stdout);
        std::fprintf(stderr, "xbd_trace: %s (frame %llu)\n", ex.what(), (unsigned long long)frame);
        return 1;
    }
    return 0;
}
