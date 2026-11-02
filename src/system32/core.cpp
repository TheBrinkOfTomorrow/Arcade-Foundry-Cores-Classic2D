// OutRunners (System Multi 32) or, with C2D_RADR, Rad Rally (System 32) as
// an afcore library (include/afcore.h), driven a frame at a time by the
// front end. OutRunners: one cabinet, player 1's monitor and speaker;
// player 2's controls stay idle.
#include "afcore.h"

#include "system32/board.h"
#include "system32/rom_import.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace v60n {
void run_native(v60::Core &c, NativeEnv &env);
}

#ifndef C2D_CORE_VERSION
#define C2D_CORE_VERSION AFCORE_MAKE_VERSION(1, 0, 0)
#endif

struct afcore {
    std::unique_ptr<s32::Board> board;
    std::string save_dir, error;
    std::vector<uint32_t> screen = std::vector<uint32_t>(s32::Video::kWidth * s32::Video::kHeight, 0xff000000u);
    std::vector<float> audio[2];
    size_t audio_pos[2]{};
    bool high_gear = false; // Rad Rally's two-position shifter
};

namespace {

#ifdef C2D_RADR
constexpr const char *kSet = s32::kRadrRomSet, *kTitle = "Rad Rally";
// segas32.cpp's routes: both YM3438s at 0.30 and the RF5C68 at 0.40, stereo
constexpr float kYmGain = 0.30f, kPcmGain = 0.40f;
constexpr bool kStereo = true;
#else
constexpr const char *kSet = s32::kOrunnersRomSet, *kTitle = "OutRunners";
// segas32.cpp's routes: the YM3438's output 1 and the MultiPCM's output 1
// go to the left cabinet's speaker ("sleft"), at 0.15 and 0.35
constexpr float kYmGain = 0.15f, kPcmGain = 0.35f;
constexpr bool kStereo = false;
#endif

std::string nv_path(const afcore *c) { return c->save_dir + "/" + kSet + ".nv"; }

void copy_error(char *err, size_t n, const std::string &s) {
    if (err && n) std::snprintf(err, n, "%s", s.c_str());
}

afcore *create(const char *rom_path, const char *save_dir, char *err, size_t err_size) {
    try {
        auto c = std::make_unique<afcore>();
        c->board = std::make_unique<s32::Board>(s32::import_rom_set(rom_path ? rom_path : "", kSet), v60n::run_native);
        if (save_dir) c->save_dir = save_dir;
        // the 93C46 (settings, records): the set's image (Rad Rally) or
        // erased (0xff) the first time
        if (!c->save_dir.empty()) {
            std::ifstream in(nv_path(c.get()), std::ios::binary);
            std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), {});
            if (d.size() == c->board->eeprom.size()) std::copy(d.begin(), d.end(), c->board->eeprom.begin());
        }
        c->board->keep_audio = true;
        return c.release();
    } catch (const std::exception &e) {
        copy_error(err, err_size, e.what());
        return nullptr;
    }
}

void save(afcore *c) {
    if (!c || c->save_dir.empty() || !c->board) return;
    std::ofstream out(nv_path(c), std::ios::binary);
    out.write(reinterpret_cast<const char *>(c->board->eeprom.data()), std::streamsize(c->board->eeprom.size()));
    c->board->eeprom_dirty = false;
}

void destroy(afcore *c) {
    if (!c) return;
    save(c);
    delete c;
}

uint8_t analog(float v, float lo, float hi) {
    if (!(v == v)) v = 0;
    const float x = lo + (hi - lo) * std::clamp(v, 0.0f, 1.0f);
    return uint8_t(std::lround(x));
}

void apply_input(afcore *c, const afcore_input *in) {
    s32::Inputs &p = c->board->inputs;
    uint32_t buttons = 0;
    float steer = 0, accel = 0, brake = 0;
    if (in) {
        const uint32_t size = in->size;
        auto has = [size](size_t end) { return size >= end; };
        if (has(offsetof(afcore_input, steer) + sizeof(float))) steer = in->steer;
        if (has(offsetof(afcore_input, accel) + sizeof(float))) accel = in->accel;
        if (has(offsetof(afcore_input, brake) + sizeof(float))) brake = in->brake;
        if (has(offsetof(afcore_input, buttons) + sizeof(uint32_t))) buttons = in->buttons;
    }
    uint8_t a = 0xff, b = 0xff, s = 0xff;
#ifdef C2D_RADR
    // P1_A D0: the gear lever (a toggle in MAME); the shift buttons move it
    // to high or low
    if (buttons & AFCORE_SHIFT_UP) c->high_gear = true;
    if (buttons & AFCORE_SHIFT_DOWN) c->high_gear = false;
    if (c->high_gear) a &= ~0x01;
#else
    // P1_A: D0 shift up, D1 shift down; P2_A: D0 DJ/music, D1 <<, D2 >>
    if (buttons & AFCORE_SHIFT_UP) a &= ~0x01;
    if (buttons & AFCORE_SHIFT_DOWN) a &= ~0x02;
    if (buttons & AFCORE_VIEW1) b &= ~0x01;
    if (buttons & AFCORE_BUTTON2) b &= ~0x02;
    if (buttons & AFCORE_BUTTON3) b &= ~0x04;
#endif
    // SERVICE12_A: D0 service, D1 test, D2 coin 1, D4 start 1
    if (buttons & AFCORE_SERVICE) s &= ~0x01;
    if (buttons & AFCORE_TEST) s &= ~0x02;
    if (buttons & AFCORE_COIN) s &= ~0x04;
    if (buttons & AFCORE_START) s &= ~0x10;
    p.p1_a = a;
    p.p2_a = b;
    p.service12_a = s;
    // ANALOG1 the wheel (a paddle, 0x80 centred), ANALOG2 gas, ANALOG3 brake
    p.analog[0] = analog((std::clamp(steer, -1.0f, 1.0f) + 1.0f) * 0.5f, 0x00, 0xff);
    p.analog[1] = analog(accel, 0, 0xff);
    p.analog[2] = analog(brake, 0, 0xff);
}

void take_audio(afcore *c) {
    s32::Board &b = *c->board;
    for (int k = 0; k < 2; ++k) {
        std::vector<float> &dst = c->audio[k];
        if (c->audio_pos[k] > 0 && c->audio_pos[k] * 2 >= dst.size()) {
            dst.erase(dst.begin(), dst.begin() + std::ptrdiff_t(c->audio_pos[k]));
            c->audio_pos[k] = 0;
        }
        // OutRunners: the left cabinet's channel (each chip's output 1) on
        // both sides; Rad Rally: stereo
        if (k == 0) {
            for (size_t i = 0; i + 1 < b.ym_out.size(); i += 2) {
                const float r = float(b.ym_out[i + 1]) * (kYmGain / 32768.0f);
                dst.push_back(kStereo ? float(b.ym_out[i]) * (kYmGain / 32768.0f) : r);
                dst.push_back(r);
            }
            b.ym_out.clear();
        } else {
            for (size_t i = 0; i + 1 < b.pcm_out.size(); i += 2) {
                const float r = b.pcm_out[i + 1] * kPcmGain;
                dst.push_back(kStereo ? b.pcm_out[i] * kPcmGain : r);
                dst.push_back(r);
            }
            b.pcm_out.clear();
        }
        const size_t cap = 2 * 60000; // at most about a second
        if (dst.size() - c->audio_pos[k] > cap) c->audio_pos[k] = dst.size() - cap;
    }
}

void run_frame(afcore *c, const afcore_input *in) {
    if (!c || !c->error.empty()) return;
    apply_input(c, in);
    try {
        c->board->run_frame();
    } catch (const std::exception &e) {
        c->error = e.what();
        return;
    }
    const std::vector<uint32_t> &s = c->board->screen[0];
    for (size_t i = 0; i < s.size(); ++i) c->screen[i] = 0xff000000u | s[i];
    take_audio(c);
}

const uint32_t *screen(afcore *c, int *w, int *h) {
    if (w) *w = s32::Video::kWidth;
    if (h) *h = s32::Video::kHeight;
    return c ? c->screen.data() : nullptr;
}

double audio_rate(afcore *c, int stream) {
    if (!c) return 0;
    if (stream == 0) return c->board->ym_rate();
    if (stream == 1) return c->board->pcm_rate();
    return 0;
}

size_t audio_read(afcore *c, int stream, float *out, size_t max_frames) {
    if (!c || stream < 0 || stream > 1) return 0;
    std::vector<float> &buf = c->audio[stream];
    const size_t have = (buf.size() - c->audio_pos[stream]) / 2, n = std::min(have, max_frames);
    if (out && n) std::memcpy(out, buf.data() + c->audio_pos[stream], n * 2 * sizeof(float));
    c->audio_pos[stream] += n * 2;
    return n;
}

int link_start(afcore *, int, const char *, int, char *err, size_t err_size) {
    copy_error(err, err_size, std::string(kTitle) + " link play is not in yet");
    return -1;
}

int link_state(afcore *, int *id, int *count) {
    if (id) *id = 0;
    if (count) *count = 0;
    return AFCORE_LINK_OFF;
}

const char *error(afcore *c) { return c && !c->error.empty() ? c->error.c_str() : nullptr; }

afcore_api make_api() {
    afcore_api a{};
    a.version = AFCORE_API_VERSION;
    a.size = sizeof(afcore_api);
    a.core_version = C2D_CORE_VERSION;
    a.romset = kSet;
    a.title = kTitle;
    a.frame_hz = 60.0; // set_refresh_hz(60)
    a.width = s32::Video::kWidth;
    a.height = s32::Video::kHeight;
    a.create = create;
    a.destroy = destroy;
    a.run_frame = run_frame;
    a.screen = screen;
    a.audio_rate = audio_rate;
    a.audio_read = audio_read;
    a.save = save;
    a.link_start = link_start;
    a.link_state = link_state;
    a.error = error;
    return a;
}

} // namespace

#ifdef C2D_RADR
extern "C" AFCORE_EXPORT const afcore_api *afcore_api_radr(void) {
#else
extern "C" AFCORE_EXPORT const afcore_api *afcore_api_orunners(void) {
#endif
    static const afcore_api api = make_api();
    return &api;
}
