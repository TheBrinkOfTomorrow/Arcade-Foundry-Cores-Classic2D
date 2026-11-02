#include "fd1094/opcode_image.h"

#include "m68k/decode.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>

namespace fd1094 {

std::vector<uint8_t> opcode_image(const std::vector<uint8_t> &raw, const Fd1094 &fd, const std::vector<std::pair<uint32_t, uint8_t>> &pcs,
                                  ImageReport *report) {
    const size_t words = raw.size() / 2;
    auto raw_word = [&](uint32_t w) { return uint16_t(raw[2 * w] << 8 | raw[2 * w + 1]); };
    std::vector<int> state_of(words, -1);
    std::map<uint8_t, size_t> count;
    ImageReport rep;
    for (const auto &[pc, st] : pcs) {
        if (pc & 1 || pc / 2 >= words) continue;
        ++count[st];
        ++rep.listed;
        m68k::Insn in;
        const uint8_t s = st;
        const bool ok = m68k::decode(pc, [&](uint32_t a) -> std::optional<uint16_t> {
            a &= 0xffffff;
            if (a / 2 >= words) return std::nullopt;
            return fd.decrypt_one(a / 2, raw_word(a / 2), s, a < 8);
        }, in);
        const uint32_t len = ok && in.length ? in.length : 2;
        for (uint32_t w = pc / 2; w < (pc + len) / 2 && w < words; ++w) {
            if (state_of[w] >= 0 && state_of[w] != st) ++rep.conflicts;
            else state_of[w] = st;
        }
    }
    for (const auto &[s, n] : count) rep.states.emplace_back(s, n);
    if (!count.empty())
        rep.default_state = std::max_element(count.begin(), count.end(), [](auto &a, auto &b) { return a.second < b.second; })->first;
    // words no run executed (branches not taken, code after them) take the
    // state of the nearest executed word before them in the same stretch of
    // code (a routine runs in one state), else the most common state
    std::vector<int> fill(state_of);
    {
        int last = -1;
        uint32_t last_w = 0;
        for (uint32_t w = 0; w < words; ++w) {
            if (state_of[w] >= 0) {
                last = state_of[w];
                last_w = w;
            } else if (last >= 0 && w - last_w <= 0x200)
                fill[w] = last;
        }
    }
    std::vector<uint8_t> img(raw.size());
    for (uint32_t w = 0; w < words; ++w) {
        const uint8_t s = w < 4 ? 0 : fill[w] >= 0 ? uint8_t(fill[w]) : rep.default_state;
        const uint16_t v = fd.decrypt_one(w, raw_word(w), s, w < 4);
        img[2 * w] = uint8_t(v >> 8);
        img[2 * w + 1] = uint8_t(v);
    }
    if (report) *report = rep;
    return img;
}

std::vector<std::pair<uint32_t, uint8_t>> read_state_seeds(const std::string &path) {
    std::vector<std::pair<uint32_t, uint8_t>> out;
    std::ifstream f(path);
    std::string l;
    while (std::getline(f, l)) {
        if (l.empty() || l[0] == '#') continue;
        std::istringstream is(l);
        std::string a, s;
        if (!(is >> a >> s)) continue;
        out.emplace_back(uint32_t(std::strtoul(a.c_str(), nullptr, 16)), uint8_t(std::strtoul(s.c_str(), nullptr, 16)));
    }
    return out;
}

} // namespace fd1094
