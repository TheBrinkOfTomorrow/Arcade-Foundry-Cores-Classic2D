// The Out Run board's cores as afcore libraries (include/afcore.h): one
// game per library (Out Run; Super Hang-On with C2D_SHANGON; Turbo Out Run
// with C2D_TOUTRUN), driven a frame at a time by the front end.
#include "afcore.h"

#include "outrun/board.h"
#include "outrun/rom_import.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstddef>
#include <cstring>
#include <exception>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#ifndef C2D_CORE_VERSION
#define C2D_CORE_VERSION AFCORE_MAKE_VERSION(1, 0, 0)
#endif

struct afcore {
    std::unique_ptr<orun::Board> board;
    std::string save_dir, error;
    std::vector<uint32_t> screen = std::vector<uint32_t>(orun::Video::kWidth * orun::Video::kHeight, 0xff000000u);
    std::vector<float> audio[2];
    size_t audio_pos[2]{};
    bool high_gear = false;
    int last_gear = 0;
    uint32_t last_buttons = 0;
};

namespace {

#ifdef C2D_SHANGON
constexpr orun::Game kGame = orun::Game::shangon;
constexpr const char *kSet = orun::kShangonRomSet, *kTitle = "Super Hang-On";
#elif defined(C2D_TOUTRUN)
constexpr orun::Game kGame = orun::Game::toutrun;
constexpr const char *kSet = orun::kToutrunRomSet, *kTitle = "Turbo Out Run";
#else
constexpr orun::Game kGame = orun::Game::outrun;
constexpr const char *kSet = orun::kRomSet, *kTitle = "Out Run";
#endif
constexpr float kYmGain = 0.30f, kPcmGain = 0.70f; // segaorun.cpp's speaker routes

std::string nv_path(const afcore *c) { return c->save_dir + "/" + kSet + ".nv"; }

void copy_error(char *err, size_t n, const std::string &s) {
    if (err && n) std::snprintf(err, n, "%s", s.c_str());
}

afcore *create(const char *rom_path, const char *save_dir, char *err, size_t err_size) {
    try {
        auto c = std::make_unique<afcore>();
        c->board = std::make_unique<orun::Board>(orun::import_rom_set(rom_path ? rom_path : "", kGame));
        if (save_dir) c->save_dir = save_dir;
        // the work RAM is the board's NVRAM (scores, settings): all 0 the first time
        if (!c->save_dir.empty()) {
            std::ifstream in(nv_path(c.get()), std::ios::binary);
            std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), {});
            if (d.size() == c->board->workram.size()) std::copy(d.begin(), d.end(), c->board->workram.begin());
        }
        c->board->sound().keep_audio = true;
        return c.release();
    } catch (const std::exception &e) {
        copy_error(err, err_size, e.what());
        return nullptr;
    }
}

void save(afcore *c) {
    if (!c || c->save_dir.empty() || !c->board) return;
    std::ofstream out(nv_path(c), std::ios::binary);
    out.write(reinterpret_cast<const char *>(c->board->workram.data()), std::streamsize(c->board->workram.size()));
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
    orun::Inputs &p = c->board->in;
    uint32_t buttons = 0;
    float steer = 0, accel = 0, brake = 0;
    int gear = 0;
    if (in) {
        const uint32_t size = in->size;
        auto has = [size](size_t end) { return size >= end; };
        if (has(offsetof(afcore_input, steer) + sizeof(float))) steer = in->steer;
        if (has(offsetof(afcore_input, accel) + sizeof(float))) accel = in->accel;
        if (has(offsetof(afcore_input, brake) + sizeof(float))) brake = in->brake;
        if (has(offsetof(afcore_input, gear) + sizeof(int))) gear = in->gear;
        if (has(offsetof(afcore_input, buttons) + sizeof(uint32_t))) buttons = in->buttons;
    }
    if (kGame == orun::Game::shangon) {
        // UNKNOWN: D0 coin 1, D2 test, D3 service, D4 start, D5 supercharger (active low)
        uint8_t u = 0xff;
        if (buttons & AFCORE_COIN) u &= ~0x01;
        if (buttons & AFCORE_TEST) u &= ~0x04;
        if (buttons & AFCORE_SERVICE) u &= ~0x08;
        if (buttons & AFCORE_START) u &= ~0x10;
        if (buttons & (AFCORE_TRIGGER1 | AFCORE_SHIFT_UP)) u &= ~0x20;
        p.unknown = u;
        // handlebar 0xe0 (left) .. 0x20 (right): PORT_REVERSE; throttle and brake 0 .. 0xff
        p.adc[0] = analog((1.0f - std::clamp(steer, -1.0f, 1.0f)) * 0.5f, 0x20, 0xe0);
        p.adc[1] = analog(accel, 0, 0xff);
        p.adc[2] = analog(brake, 0, 0xff);
        c->last_buttons = buttons;
        return;
    }
    // the gear lever (low/high): SHIFT_UP/SHIFT_DOWN, or gear 1 low and 2 up high
    const uint32_t pressed = buttons & ~c->last_buttons;
    if (pressed & AFCORE_SHIFT_UP) c->high_gear = true;
    if (pressed & AFCORE_SHIFT_DOWN) c->high_gear = false;
    if (gear != c->last_gear && gear > 0) c->high_gear = gear >= 2;
    c->last_gear = gear;
    c->last_buttons = buttons;
    // SERVICE: D1 test, D2 service, D3 start, D6 coin 1 (active low), D4 gear (active high)
    uint8_t s = 0xef;
    if (buttons & AFCORE_TEST) s &= ~0x02;
    if (buttons & AFCORE_SERVICE) s &= ~0x04;
    if (buttons & AFCORE_START) s &= ~0x08;
    if (buttons & AFCORE_COIN) s &= ~0x40;
    if (c->high_gear) s |= 0x10;
    // Turbo Out Run: D5 turbo (active low)
    if (kGame == orun::Game::toutrun && (buttons & AFCORE_TRIGGER1)) s &= ~0x20;
    p.service = s;
    // steering 0x20 (left) .. 0xe0 (right), pedals 0 .. 0xff
    p.adc[0] = analog((std::clamp(steer, -1.0f, 1.0f) + 1.0f) * 0.5f, 0x20, 0xe0);
    p.adc[1] = analog(accel, 0, 0xff);
    p.adc[2] = analog(brake, 0, 0xff);
}

void take_audio(afcore *c) {
    orun::Sound &snd = c->board->sound();
    const std::pair<std::vector<int32_t> *, float> src[2] = {{&snd.ym_out, kYmGain}, {&snd.pcm_out, kPcmGain}};
    for (int k = 0; k < 2; ++k) {
        std::vector<float> &dst = c->audio[k];
        if (c->audio_pos[k] > 0 && c->audio_pos[k] * 2 >= dst.size()) {
            dst.erase(dst.begin(), dst.begin() + std::ptrdiff_t(c->audio_pos[k]));
            c->audio_pos[k] = 0;
        }
        // keep at most a second: a front end that stops reading loses the oldest
        const size_t cap = 2 * 62500;
        for (int32_t v : *src[k].first) dst.push_back(float(v) * (src[k].second / 32768.0f));
        src[k].first->clear();
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
    if (orun::Video *v = c->board->video())
        for (size_t i = 0; i < v->bitmap.size(); ++i) c->screen[i] = 0xff000000u | v->rgb(v->bitmap[i]);
    take_audio(c);
}

const uint32_t *screen(afcore *c, int *w, int *h) {
    if (w) *w = orun::Video::kWidth;
    if (h) *h = orun::Video::kHeight;
    return c ? c->screen.data() : nullptr;
}

double audio_rate(afcore *c, int stream) {
    if (!c) return 0;
    if (stream == 0) return c->board->sound().ym_rate();
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
    a.frame_hz = 6293700.0 / (400.0 * 262.0); // set_raw(6293700, 400, 0, 320, 262, 0, 224)
    a.width = orun::Video::kWidth;
    a.height = orun::Video::kHeight;
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

#if defined(C2D_SHANGON)
extern "C" AFCORE_EXPORT const afcore_api *afcore_api_shangon(void) {
#elif defined(C2D_TOUTRUN)
extern "C" AFCORE_EXPORT const afcore_api *afcore_api_toutrun(void) {
#else
extern "C" AFCORE_EXPORT const afcore_api *afcore_api_outrun(void) {
#endif
    static const afcore_api api = make_api();
    return &api;
}
