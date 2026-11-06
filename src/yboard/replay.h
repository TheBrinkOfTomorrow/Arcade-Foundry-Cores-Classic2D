// Input replays for checking the Y Board core against MAME: "UPDATE PORT
// VALUE" lines (PORT: p1, general, limitsw, dsw, coinage, adc0-adc5; VALUE
// hex; '#' starts a comment). After screen update UPDATE+1 the port reads
// VALUE. MAME's trace patch reads the same file (ORTRACE_REPLAY); its
// update count applies UPDATE at the end of its update UPDATE+1, and so
// does ybd_trace.
#pragma once

#include "yboard/board.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace ybd {

class Replay {
public:
    bool load(const char *path) {
        FILE *f = std::fopen(path, "r");
        if (!f) return false;
        static const char *const names[11] = {"p1", "general", "limitsw", "dsw", "coinage", "adc0", "adc1", "adc2", "adc3", "adc4", "adc5"};
        char line[256];
        while (std::fgets(line, sizeof line, f)) {
            unsigned long long u;
            char name[32];
            unsigned v;
            if (line[0] == '#' || std::sscanf(line, "%llu %31s %x", &u, name, &v) != 3) continue;
            for (int i = 0; i < 11; ++i)
                if (!std::strcmp(name, names[i])) ev_.push_back({u, i, uint8_t(v)});
        }
        std::fclose(f);
        return true;
    }

    void apply(uint64_t update, Inputs &in) {
        for (; pos_ < ev_.size() && ev_[pos_].update <= update; ++pos_) {
            const Ev &e = ev_[pos_];
            switch (e.port) {
            case 0: in.p1 = e.value; break;
            case 1: in.general = e.value; break;
            case 2: in.limitsw = e.value; break;
            case 3: in.dsw = e.value; break;
            case 4: in.coinage = e.value; break;
            default: in.adc[e.port - 5] = e.value; break;
            }
        }
    }

private:
    struct Ev {
        uint64_t update;
        int port;
        uint8_t value;
    };
    std::vector<Ev> ev_;
    size_t pos_ = 0;
};

} // namespace ybd
