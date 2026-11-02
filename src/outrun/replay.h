// Input replays for checking the core against MAME: a text file of
// "UPDATE PORT VALUE" lines (PORT: service, unknown, coinage, dsw, adc0,
// adc1, adc2; VALUE hex; '#' starts a comment). After screen update UPDATE
// the port reads VALUE. MAME's trace patch reads the same file
// (ORTRACE_REPLAY); its update count has one extra power-on update.
#pragma once

#include "outrun/board.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace orun {

class Replay {
public:
    bool load(const char *path) {
        FILE *f = std::fopen(path, "r");
        if (!f) return false;
        static const char *const names[7] = {"service", "unknown", "coinage", "dsw", "adc0", "adc1", "adc2"};
        char line[256];
        while (std::fgets(line, sizeof line, f)) {
            unsigned long long u;
            char name[32];
            unsigned v;
            if (line[0] == '#' || std::sscanf(line, "%llu %31s %x", &u, name, &v) != 3) continue;
            for (int i = 0; i < 7; ++i)
                if (!std::strcmp(name, names[i])) ev_.push_back({u, i, uint8_t(v)});
        }
        std::fclose(f);
        return true;
    }

    // apply the events due after screen update `update`
    void apply(uint64_t update, Inputs &in) {
        for (; pos_ < ev_.size() && ev_[pos_].update <= update; ++pos_) {
            const Ev &e = ev_[pos_];
            switch (e.port) {
            case 0: in.service = e.value; break;
            case 1: in.unknown = e.value; break;
            case 2: in.coinage = e.value; break;
            case 3: in.dsw = e.value; break;
            default: in.adc[e.port - 4] = e.value; break;
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

} // namespace orun
