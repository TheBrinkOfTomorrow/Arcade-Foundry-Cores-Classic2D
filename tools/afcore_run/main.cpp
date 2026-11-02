// afcore_run ROMSET FRAMES [--script FILE] [--dump-every N DIR] [--audio FILE]
// Runs the core through its library interface, headless. The script is
// "FRAME KEY VALUE" lines (KEY: coin, start, test, service, shift_up,
// shift_down, fire, missile as 0/1; steer, stick_y -1..1; accel, brake 0..1). Prints an FNV hash of
// each screen and the speed; --audio writes each stream's floats.
#include "afcore.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#ifndef AFCORE_SET
#define AFCORE_SET outrun
#endif
#define AFCORE_CAT2(a, b) a##b
#define AFCORE_CAT(a, b) AFCORE_CAT2(a, b)
extern "C" const afcore_api *AFCORE_CAT(afcore_api_, AFCORE_SET)(void);

int main(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: afcore_run ROMSET FRAMES [--script FILE] [--dump-every N DIR] [--audio FILE]\n");
        return 2;
    }
    const afcore_api *api = AFCORE_CAT(afcore_api_, AFCORE_SET)();
    const long frames = std::strtol(argv[2], nullptr, 10);
    std::multimap<long, std::pair<std::string, float>> script;
    long dump_every = 0;
    std::string dump_dir, audio_path;
    for (int i = 3; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--script") && i + 1 < argc) {
            std::ifstream f(argv[++i]);
            long fr;
            std::string k;
            float v;
            while (f >> fr >> k >> v) script.insert({fr, {k, v}});
        } else if (!std::strcmp(argv[i], "--dump-every") && i + 2 < argc) {
            dump_every = std::strtol(argv[++i], nullptr, 10);
            dump_dir = argv[++i];
        } else if (!std::strcmp(argv[i], "--audio") && i + 1 < argc)
            audio_path = argv[++i];
    }
    char err[512] = "";
    afcore *c = api->create(argv[1], nullptr, err, sizeof err);
    if (!c) {
        std::fprintf(stderr, "afcore_run: %s\n", err);
        return 1;
    }
    std::printf("%s (%s) %dx%d %.4f Hz, audio %.1f + %.1f Hz\n", api->title, api->romset, api->width, api->height, api->frame_hz,
                api->audio_rate(c, 0), api->audio_rate(c, 1));
    afcore_input in{};
    in.size = sizeof in;
    std::map<std::string, uint32_t> bits = {{"coin", AFCORE_COIN},         {"start", AFCORE_START},          {"test", AFCORE_TEST},
                                            {"service", AFCORE_SERVICE},   {"shift_up", AFCORE_SHIFT_UP},    {"shift_down", AFCORE_SHIFT_DOWN}};
    std::vector<float> pcm(2 * 8192), mix;
    std::FILE *af = audio_path.empty() ? nullptr : std::fopen(audio_path.c_str(), "wb");
    const auto t0 = std::chrono::steady_clock::now();
    for (long f = 0; f < frames; ++f) {
        auto [a, b] = script.equal_range(f);
        for (auto it = a; it != b; ++it) {
            const auto &[k, v] = it->second;
            if (bits.count(k)) in.buttons = v ? (in.buttons | bits[k]) : (in.buttons & ~bits[k]);
            else if (k == "steer") in.steer = v;
            else if (k == "accel") in.accel = v;
            else if (k == "brake") in.brake = v;
        }
        api->run_frame(c, &in);
        if (const char *e = api->error(c)) {
            std::fprintf(stderr, "afcore_run: frame %ld: %s\n", f, e);
            return 1;
        }
        int w, h;
        const uint32_t *px = api->screen(c, &w, &h);
        uint64_t hash = 1469598103934665603ULL;
        for (int i = 0; i < w * h; ++i) hash = (hash ^ (px[i] & 0xffffff)) * 1099511628211ULL;
        std::printf("frame %ld %016llx\n", f, (unsigned long long)hash);
        if (dump_every && f % dump_every == 0) {
            char path[1024];
            std::snprintf(path, sizeof path, "%s/f%06ld.rgb", dump_dir.c_str(), f);
            if (std::FILE *o = std::fopen(path, "wb")) {
                for (int i = 0; i < w * h; ++i) {
                    const uint8_t rgb[3] = {uint8_t(px[i] >> 16), uint8_t(px[i] >> 8), uint8_t(px[i])};
                    std::fwrite(rgb, 1, 3, o);
                }
                std::fclose(o);
            }
        }
        for (int s = 0; s < 2; ++s) {
            const size_t n = api->audio_read(c, s, pcm.data(), pcm.size() / 2);
            if (af) {
                const uint32_t hdr[2] = {uint32_t(s), uint32_t(n)};
                std::fwrite(hdr, sizeof hdr, 1, af);
                std::fwrite(pcm.data(), sizeof(float), 2 * n, af);
            }
        }
    }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::fprintf(stderr, "afcore_run: %ld frames in %.2f s (%.1f fps, %.1fx real time)\n", frames, secs, frames / secs,
                 frames / secs / api->frame_hz);
    if (af) std::fclose(af);
    api->destroy(c);
    return 0;
}
