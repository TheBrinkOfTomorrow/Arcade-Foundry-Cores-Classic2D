// The Hang-On board cores as afcore libraries (common/include/afcore.h), driven a
// frame at a time by the front end: Hang-On, Space Harrier with C2D_SHARRIER
// or Enduro Racer with C2D_ENDUROR.
#include "afcore.h"

#include "hangon/board.h"
#include "hangon/rom_import.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstddef>
#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <vector>

#ifndef C2D_CORE_VERSION
#define C2D_CORE_VERSION AFCORE_MAKE_VERSION(1, 0, 0)
#endif

struct afcore {
    std::unique_ptr<hang::Board> board;
    std::string error;
    std::vector<uint32_t> screen = std::vector<uint32_t>(hang::Video::kWidth * hang::Video::kHeight, 0xff000000u);
    std::vector<float> audio[2];
    size_t audio_pos[2]{};
    // the YM2203's 1 MHz stream, averaged down by kYmDecim
    double ym_acc = 0;
    int ym_n = 0;
};

namespace {

#if defined(C2D_SHARRIER)
constexpr const char *kSet = hang::kSharrierRomSet, *kTitle = "Space Harrier";
constexpr hang::Game kGame = hang::Game::sharrier;
#elif defined(C2D_ENDUROR)
constexpr const char *kSet = hang::kEndurorRomSet, *kTitle = "Enduro Racer";
constexpr hang::Game kGame = hang::Game::enduror;
#else
constexpr const char *kSet = hang::kRomSet, *kTitle = "Hang-On";
constexpr hang::Game kGame = hang::Game::hangon;
#endif
// sound_board_2203's speaker routes: SSG 0.05, FM 0.15, SegaPCM 0.40;
// sound_board_2151's (Enduro Racer): YM2151 0.30, the 315-5218 0.70
constexpr bool kOpm = kGame == hang::Game::enduror;
constexpr float kSsgGain = 0.05f, kFmGain = 0.15f, kPcmGain = kOpm ? 0.70f : 0.40f, kOpmGain = 0.30f;
constexpr int kYmDecim = 6; // 333.3 kHz (MAME's medium fidelity) -> 55.6 kHz

void copy_error(char *err, size_t n, const std::string &s) {
    if (err && n) std::snprintf(err, n, "%s", s.c_str());
}

// afcore 5.2: the loader create uses reads every file and checks its size
// and CRC; nothing is kept.
int check_roms(const char *rom_path, char *report, size_t report_size) {
    try {
        hang::import_rom_set(rom_path ? rom_path : "", kGame);
        copy_error(report, report_size, "");
        return 1;
    } catch (const std::exception &e) {
        copy_error(report, report_size, e.what());
        return 0;
    }
}

afcore *create(const char *rom_path, const char *, char *err, size_t err_size) {
    try {
        auto c = std::make_unique<afcore>();
        c->board = std::make_unique<hang::Board>(hang::import_rom_set(rom_path ? rom_path : "", kGame));
        c->board->sound().keep_audio = true;
        return c.release();
    } catch (const std::exception &e) {
        copy_error(err, err_size, e.what());
        return nullptr;
    }
}

void save(afcore *) {} // no NVRAM: the settings are DIP switches

void destroy(afcore *c) { delete c; }

uint8_t analog(float v, float lo, float hi) {
    if (!(v == v)) v = 0;
    const float x = lo + (hi - lo) * std::clamp(v, 0.0f, 1.0f);
    return uint8_t(std::lround(x));
}

void apply_input(afcore *c, const afcore_input *in) {
    hang::Inputs &p = c->board->in;
    uint32_t buttons = 0;
    float steer = 0, accel = 0, brake = 0, stick_y = 0;
    if (in) {
        const uint32_t size = in->size;
        auto has = [size](size_t end) { return size >= end; };
        if (has(offsetof(afcore_input, steer) + sizeof(float))) steer = in->steer;
        if (has(offsetof(afcore_input, accel) + sizeof(float))) accel = in->accel;
        if (has(offsetof(afcore_input, brake) + sizeof(float))) brake = in->brake;
        if (has(offsetof(afcore_input, buttons) + sizeof(uint32_t))) buttons = in->buttons;
        if (has(offsetof(afcore_input, stick_y) + sizeof(float))) stick_y = in->stick_y;
    }
    // SERVICE: D0 coin 1, D2 test, D3 service, D4 start (active low)
    uint8_t s = 0xff;
    if (buttons & AFCORE_COIN) s &= ~0x01;
    if (buttons & AFCORE_TEST) s &= ~0x04;
    if (buttons & AFCORE_SERVICE) s &= ~0x08;
    if (buttons & AFCORE_START) s &= ~0x10;
#if defined(C2D_ENDUROR)
    // SERVICE: D0 coin 1, D2 test, D3 service, D6 start (D4 unused); ADC0
    // the gas and ADC1 the brake 0 .. 0xff, ADC2 the wheelie (bank up/down:
    // 0x20 at rest, pulling the stick back raises it to 0xff, pushing lowers
    // it to 0), ADC3 the handlebar (PORT_REVERSE) 0xff left .. 0x01 right
    s = 0xff;
    if (buttons & AFCORE_COIN) s &= ~0x01;
    if (buttons & AFCORE_TEST) s &= ~0x04;
    if (buttons & AFCORE_SERVICE) s &= ~0x08;
    if (buttons & AFCORE_START) s &= ~0x40;
    p.service = s;
    p.adc[0] = analog(accel, 0, 0xff);
    p.adc[1] = analog(brake, 0, 0xff);
    const float y = std::clamp(stick_y, -1.0f, 1.0f);
    p.adc[2] = y >= 0 ? analog(y, 0x20, 0xff) : analog(y + 1.0f, 0x00, 0x20);
    p.adc[3] = analog((1.0f - std::clamp(steer, -1.0f, 1.0f)) * 0.5f, 0x01, 0xff);
    return;
#elif defined(C2D_SHARRIER)
    // D5-D7 buttons 1-3 (fire on any)
    if (buttons & AFCORE_TRIGGER1) s &= ~0x20;
    if (buttons & AFCORE_BUTTON2) s &= ~0x40;
    if (buttons & AFCORE_BUTTON3) s &= ~0x80;
    p.service = s;
    // the stick (both axes PORT_REVERSE): X 0xe0 left .. 0x20 right, Y 0xe0
    // up (stick_y -1) .. 0x20 down
    p.adc[0] = analog((1.0f - std::clamp(steer, -1.0f, 1.0f)) * 0.5f, 0x20, 0xe0);
    p.adc[1] = analog((1.0f - std::clamp(stick_y, -1.0f, 1.0f)) * 0.5f, 0x20, 0xe0);
    (void)accel;
    (void)brake;
    return;
#endif
    p.service = s;
    // handlebar 0xe0 (left) .. 0x20 (right): PORT_REVERSE; throttle and brake 0 .. 0xff
    p.adc[0] = analog((1.0f - std::clamp(steer, -1.0f, 1.0f)) * 0.5f, 0x20, 0xe0);
    p.adc[1] = analog(accel, 0, 0xff);
    p.adc[2] = analog(brake, 0, 0xff);
}

void push(afcore *c, int k, float l, float r) {
    std::vector<float> &dst = c->audio[k];
    dst.push_back(l);
    dst.push_back(r);
}

void take_audio(afcore *c) {
    hang::Sound &snd = c->board->sound();
    for (int k = 0; k < 2; ++k) {
        std::vector<float> &dst = c->audio[k];
        if (c->audio_pos[k] > 0 && c->audio_pos[k] * 2 >= dst.size()) {
            dst.erase(dst.begin(), dst.begin() + std::ptrdiff_t(c->audio_pos[k]));
            c->audio_pos[k] = 0;
        }
    }
    const std::vector<int32_t> &ym = snd.ym_out;
    if (kOpm) // YM2151: stereo at its own 62.5 kHz
        for (size_t i = 0; i + 1 < ym.size(); i += 2)
            push(c, 0, float(ym[i]) * (kOpmGain / 32768.0f), float(ym[i + 1]) * (kOpmGain / 32768.0f));
    // YM2203: FM, SSG A-C per sample, mono to both sides
    for (size_t i = 0; !kOpm && i + 3 < ym.size(); i += 4) {
        c->ym_acc += double(ym[i]) * kFmGain + double(ym[i + 1] + ym[i + 2] + ym[i + 3]) * kSsgGain;
        if (++c->ym_n == kYmDecim) {
            const float v = float(c->ym_acc / kYmDecim / 32768.0);
            push(c, 0, v, v);
            c->ym_acc = 0;
            c->ym_n = 0;
        }
    }
    snd.ym_out.clear();
    for (size_t i = 0; i + 1 < snd.pcm_out.size(); i += 2)
        push(c, 1, float(snd.pcm_out[i]) * (kPcmGain / 32768.0f), float(snd.pcm_out[i + 1]) * (kPcmGain / 32768.0f));
    snd.pcm_out.clear();
    // keep at most a second: a front end that stops reading loses the oldest
    const size_t cap = 2 * 62500;
    for (int k = 0; k < 2; ++k)
        if (c->audio[k].size() - c->audio_pos[k] > cap) c->audio_pos[k] = c->audio[k].size() - cap;
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
    if (hang::Video *v = c->board->video())
        for (size_t i = 0; i < v->bitmap.size(); ++i) c->screen[i] = 0xff000000u | v->rgb(v->bitmap[i]);
    take_audio(c);
}

const uint32_t *screen(afcore *c, int *w, int *h) {
    if (w) *w = hang::Video::kWidth;
    if (h) *h = hang::Video::kHeight;
    return c ? c->screen.data() : nullptr;
}

double audio_rate(afcore *c, int stream) {
    if (!c) return 0;
    if (stream == 0) return c->board->sound().ym_rate() / (kOpm ? 1 : kYmDecim);
    if (stream == 1) return c->board->sound().pcm_rate();
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
    copy_error(err, err_size, std::string(kTitle) + " has no link play");
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
    a.frame_hz = 6293700.0 / (400.0 * 262.0); // set_raw(25.1748 MHz / 4, 400, 0, 320, 262, 0, 224)
    a.width = hang::Video::kWidth;
    a.height = hang::Video::kHeight;
    a.create = create;
    a.check_roms = check_roms;
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

#if defined(C2D_SHARRIER)
extern "C" AFCORE_EXPORT const afcore_api *afcore_api_sharrier(void) {
#elif defined(C2D_ENDUROR)
extern "C" AFCORE_EXPORT const afcore_api *afcore_api_enduror(void) {
#else
extern "C" AFCORE_EXPORT const afcore_api *afcore_api_hangon(void) {
#endif
    static const afcore_api api = make_api();
    return &api;
}
