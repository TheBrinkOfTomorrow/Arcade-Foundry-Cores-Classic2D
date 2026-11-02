#include "runtime/rom_source.h"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <sys/stat.h>

namespace rt {

namespace {
bool is_dir(const std::string &p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool equal_nocase(const std::string &a, const char *b) {
    size_t i = 0;
    for (; i < a.size() && b[i]; ++i)
        if (std::tolower(uint8_t(a[i])) != std::tolower(uint8_t(b[i]))) return false;
    return i == a.size() && !b[i];
}
} // namespace

RomSource::RomSource(const std::string &path) : dir_(is_dir(path) ? path : "") {
    if (dir_.empty()) {
        try {
            zip_ = std::make_unique<Zip>(path);
        } catch (const std::exception &e) {
            throw RomError(path + ": " + e.what());
        }
    }
}

std::vector<uint8_t> RomSource::get(const RomFile &f) const {
    std::vector<uint8_t> d;
    if (zip_) {
        // MAME names are matched without case; a CRC match under another
        // name also counts, and wins over a same-named file with another CRC
        // (a merged set holds its clones' files too)
        std::string found;
        bool found_good = false;
        for (const auto &[name, e] : zip_->entries()) {
            std::string base = name.substr(name.find_last_of('/') + 1);
            if (equal_nocase(base, f.name)) {
                found = name;
                found_good = e.crc == f.crc;
            }
        }
        if (!found_good)
            for (const auto &[name, e] : zip_->entries())
                if (e.crc == f.crc && e.usize == f.size) found = name;
        if (found.empty()) throw RomError(std::string(f.name) + " is missing from the ROM set");
        try {
            d = zip_->read(found);
        } catch (const std::exception &e) {
            throw RomError(std::string(f.name) + ": " + e.what());
        }
    } else {
        std::ifstream in(dir_ + "/" + f.name, std::ios::binary);
        if (!in) throw RomError(std::string(f.name) + " is missing from " + dir_);
        d.assign(std::istreambuf_iterator<char>(in), {});
    }
    if (d.size() != f.size) throw RomError(std::string(f.name) + " has the wrong size");
    if (crc32(d.data(), d.size()) != f.crc) {
        char buf[160];
        std::snprintf(buf, sizeof buf, "%s has the wrong CRC (a bad dump or another version; expected %08x)", f.name, f.crc);
        throw RomError(buf);
    }
    return d;
}

std::vector<uint8_t> RomSource::load16_byte(const RomFile *files, int pairs, size_t region) const {
    std::vector<uint8_t> img(region, 0);
    size_t base = 0;
    for (int p = 0; p < pairs; ++p) {
        const auto e = get(files[p * 2]), o = get(files[p * 2 + 1]);
        for (size_t i = 0; i < e.size() && base + 2 * i + 1 < region; ++i) {
            img[base + 2 * i] = e[i];
            img[base + 2 * i + 1] = o[i];
        }
        base += 2 * e.size();
    }
    return img;
}

std::vector<uint8_t> RomSource::load32_byte(const RomFile *files, int groups, size_t region) const {
    std::vector<uint8_t> img(region, 0);
    size_t base = 0;
    for (int g = 0; g < groups; ++g) {
        size_t sz = 0;
        for (int lane = 0; lane < 4; ++lane) {
            const auto d = get(files[g * 4 + lane]);
            sz = d.size();
            for (size_t i = 0; i < d.size() && base + 4 * i + size_t(lane) < region; ++i) img[base + 4 * i + size_t(lane)] = d[i];
        }
        base += 4 * sz;
    }
    return img;
}

} // namespace rt
