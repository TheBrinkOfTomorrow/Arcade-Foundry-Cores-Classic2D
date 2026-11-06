// The Y Board cores as afcore libraries (common/include/afcore.h): one game per
// library (Galaxy Force 2; Power Drift with C2D_PDRIFT; G-LOC with C2D_GLOC;
// Strike Fighter with C2D_STRKFGTR; Rail Chase with C2D_RCHASE), driven a frame
// at a time by the front end.
#include "afcore.h"

#include "yboard/board.h"
#include "yboard/rom_import.h"

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

#ifndef C2D_CORE_VERSION
#define C2D_CORE_VERSION AFCORE_MAKE_VERSION(1, 0, 0)
#endif

struct afcore {
    std::unique_ptr<ybd::Board> board;
    std::string save_dir, error;
    std::vector<uint32_t> screen = std::vector<uint32_t>(ybd::Video::kWidth * ybd::Video::kHeight, 0xff000000u);
    std::vector<float> audio[2];
    size_t audio_pos[2]{};
    bool high_gear = false; // Power Drift's shifter (MAME: a toggle)
    float aim[2][2] = {{0.5f, 0.5f}, {0.5f, 0.5f}}; // Rail Chase's guns, last on-screen aim
};

namespace {

#if defined(C2D_PDRIFT)
constexpr ybd::Game kGame = ybd::Game::pdrift;
constexpr const char *kSet = ybd::kPdriftRomSet, *kTitle = "Power Drift";
#elif defined(C2D_GLOC)
constexpr ybd::Game kGame = ybd::Game::gloc;
constexpr const char *kSet = ybd::kGlocRomSet, *kTitle = "G-LOC Air Battle";
#elif defined(C2D_STRKFGTR)
constexpr ybd::Game kGame = ybd::Game::strkfgtr;
constexpr const char *kSet = ybd::kStrkfgtrRomSet, *kTitle = "Strike Fighter";
#elif defined(C2D_RCHASE)
constexpr ybd::Game kGame = ybd::Game::rchase;
constexpr const char *kSet = ybd::kRchaseRomSet, *kTitle = "Rail Chase";
#else
constexpr ybd::Game kGame = ybd::Game::gforce2;
constexpr const char *kSet = ybd::kGforce2RomSet, *kTitle = "Galaxy Force 2";
#endif

constexpr float kYmGain = 0.30f, kPcmGain = 0.70f; // segaybd.cpp's speaker routes

std::string nv_path(const afcore *c) { return c->save_dir + "/" + kSet + ".nv"; }

void copy_error(char *err, size_t n, const std::string &s) {
    if (err && n) std::snprintf(err, n, "%s", s.c_str());
}

// afcore 5.2: the loader create uses reads every file and checks its size
// and CRC; nothing is kept.
int check_roms(const char *rom_path, char *report, size_t report_size) {
    try {
        ybd::import_rom_set(rom_path ? rom_path : "", kGame);
        copy_error(report, report_size, "");
        return 1;
    } catch (const std::exception &e) {
        copy_error(report, report_size, e.what());
        return 0;
    }
}

afcore *create(const char *rom_path, const char *save_dir, char *err, size_t err_size) {
    try {
        auto c = std::make_unique<afcore>();
        c->board = std::make_unique<ybd::Board>(ybd::import_rom_set(rom_path ? rom_path : "", kGame));
        if (save_dir) c->save_dir = save_dir;
        // the X CPU's backup RAM is the board's NVRAM (scores, settings): all 0 the first time
        if (!c->save_dir.empty()) {
            std::ifstream in(nv_path(c.get()), std::ios::binary);
            std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), {});
            if (d.size() == c->board->backupram.size()) std::copy(d.begin(), d.end(), c->board->backupram.begin());
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
    out.write(reinterpret_cast<const char *>(c->board->backupram.data()), std::streamsize(c->board->backupram.size()));
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
    ybd::Inputs &p = c->board->in;
    uint32_t buttons = 0;
    float steer = 0, accel = 0, brake = 0, stick_y = 0;
    float gun_x[2] = {0.5f, 0.5f}, gun_y[2] = {0.5f, 0.5f};
    uint32_t buttons2 = 0;
    if (in) {
        const uint32_t size = in->size;
        auto has = [size](size_t end) { return size >= end; };
        if (has(offsetof(afcore_input, steer) + sizeof(float))) steer = in->steer;
        if (has(offsetof(afcore_input, accel) + sizeof(float))) accel = in->accel;
        if (has(offsetof(afcore_input, brake) + sizeof(float))) brake = in->brake;
        if (has(offsetof(afcore_input, buttons) + sizeof(uint32_t))) buttons = in->buttons;
        if (has(offsetof(afcore_input, stick_y) + sizeof(float))) stick_y = in->stick_y;
        if (has(offsetof(afcore_input, gun_y) + sizeof(in->gun_y)))
            for (int i = 0; i < 2; ++i) {
                gun_x[i] = in->gun_x[i];
                gun_y[i] = in->gun_y[i];
            }
        if (has(offsetof(afcore_input, buttons2) + sizeof(uint32_t))) buttons2 = in->buttons2;
    }
    if (kGame == ybd::Game::rchase) {
        // Rail Chase: GENERAL D0 player 2's trigger, D1 player 1's, D2 test,
        // D3 service, D4/D5 coins 1/2, D6/D7 starts 2/1 (active low); ADC0/1
        // player 1's gun X and Y, ADC2/3 player 2's, 0 .. 0xff across and
        // down. The front ends' gun convention: TRIGGER1 player 1's
        // trigger, TRIGGER2 or buttons2's TRIGGER1 player 2's. A gun aimed
        // off screen (the front ends' reload) keeps its last aim.
        uint8_t g = 0xff;
        if (buttons & AFCORE_TRIGGER1) g &= ~0x02;
        if ((buttons & AFCORE_TRIGGER2) || (buttons2 & AFCORE_TRIGGER1)) g &= ~0x01;
        if (buttons & AFCORE_TEST) g &= ~0x04;
        if (buttons & AFCORE_SERVICE) g &= ~0x08;
        if (buttons & AFCORE_COIN) g &= ~0x10;
        if (buttons2 & AFCORE_COIN) g &= ~0x20;
        if ((buttons & AFCORE_START2) || (buttons2 & AFCORE_START)) g &= ~0x40;
        if (buttons & AFCORE_START) g &= ~0x80;
        p.general = g;
        for (int i = 0; i < 2; ++i) {
            if (gun_x[i] >= 0.0f && gun_x[i] <= 1.0f && gun_y[i] >= 0.0f && gun_y[i] <= 1.0f)
                c->aim[i][0] = gun_x[i], c->aim[i][1] = gun_y[i];
            p.adc[2 * i] = analog(c->aim[i][0], 0, 0xff);
            p.adc[2 * i + 1] = analog(c->aim[i][1], 0, 0xff);
        }
        return;
    }
    // GENERAL: D1 test, D2 service, D3 start, D6 coin 1 (active low)
    uint8_t g = 0xff;
    if (buttons & AFCORE_TEST) g &= ~0x02;
    if (buttons & AFCORE_SERVICE) g &= ~0x04;
    if (buttons & AFCORE_START) g &= ~0x08;
    if (buttons & AFCORE_COIN) g &= ~0x40;
    if (kGame == ybd::Game::pdrift) {
        // Power Drift: D5 the shifter, high when set (SHIFT_UP high gear,
        // SHIFT_DOWN low); ADC5 the wheel 0x20 left .. 0xe0 right, ADC4
        // the gas and ADC3 the brake 0 released .. 0xff
        if (buttons & AFCORE_SHIFT_UP) c->high_gear = true;
        if (buttons & AFCORE_SHIFT_DOWN) c->high_gear = false;
        if (!c->high_gear) g &= ~0x20;
        p.general = g;
        p.adc[5] = analog((std::clamp(steer, -1.0f, 1.0f) + 1.0f) * 0.5f, 0x20, 0xe0);
        p.adc[4] = analog(accel, 0, 0xff);
        p.adc[3] = analog(brake, 0, 0xff);
        return;
    }
    if (kGame == ybd::Game::gloc || kGame == ybd::Game::strkfgtr) {
        // G-LOC and Strike Fighter: D0 after burner, D4 vulcan, D5 missile; ADC3
        // the stick's Y (PORT_REVERSE) 0xc0 pushed forward .. 0x40 pulled back,
        // ADC4 the throttle 0x80 at rest (accel up to 0xff, brake down to 0x01),
        // ADC5 the stick's X 0x20 left .. 0xe0 right
        if (buttons & AFCORE_BUTTON3) g &= ~0x01;
        if (buttons & AFCORE_TRIGGER1) g &= ~0x10;
        if (buttons & AFCORE_BUTTON2) g &= ~0x20;
        p.general = g;
        p.adc[3] = analog((1.0f - std::clamp(stick_y, -1.0f, 1.0f)) * 0.5f, 0x40, 0xc0);
        p.adc[4] = analog(std::clamp(0.5f + 0.5f * accel - 0.5f * brake, 0.0f, 1.0f), 0x01, 0xff);
        p.adc[5] = analog((std::clamp(steer, -1.0f, 1.0f) + 1.0f) * 0.5f, 0x20, 0xe0);
        return;
    }
    // Galaxy Force 2: D4 shoot, D5 missile; ADC0 the stick's X 0x01 left ..
    // 0xff right, ADC1 its Y (PORT_REVERSE) 0xff pushed forward .. 0x01
    // pulled back, ADC2 the throttle 0x80 at rest, accel pushes it up to
    // 0xff, brake pulls it back to 0x01
    if (buttons & AFCORE_TRIGGER1) g &= ~0x10;
    if (buttons & AFCORE_BUTTON2) g &= ~0x20;
    p.general = g;
    p.adc[0] = analog((std::clamp(steer, -1.0f, 1.0f) + 1.0f) * 0.5f, 0x01, 0xff);
    p.adc[1] = analog((1.0f - std::clamp(stick_y, -1.0f, 1.0f)) * 0.5f, 0x01, 0xff);
    p.adc[2] = analog(std::clamp(0.5f + 0.5f * accel - 0.5f * brake, 0.0f, 1.0f), 0x01, 0xff);
}

void take_audio(afcore *c) {
    ybd::Sound &snd = c->board->sound();
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
    const ybd::Video &v = c->board->video();
    for (size_t i = 0; i < v.bitmap.size(); ++i) c->screen[i] = 0xff000000u | v.rgb(v.bitmap[i]);
    take_audio(c);
}

const uint32_t *screen(afcore *c, int *w, int *h) {
    if (w) *w = ybd::Video::kWidth;
    if (h) *h = ybd::Video::kHeight;
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
    a.frame_hz = 60.0; // set_refresh_hz(60)
    a.width = ybd::Video::kWidth;
    a.height = ybd::Video::kHeight;
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

#if defined(C2D_PDRIFT)
extern "C" AFCORE_EXPORT const afcore_api *afcore_api_pdrift(void) {
#elif defined(C2D_GLOC)
extern "C" AFCORE_EXPORT const afcore_api *afcore_api_gloc(void) {
#elif defined(C2D_STRKFGTR)
extern "C" AFCORE_EXPORT const afcore_api *afcore_api_strkfgtr(void) {
#elif defined(C2D_RCHASE)
extern "C" AFCORE_EXPORT const afcore_api *afcore_api_rchase(void) {
#else
extern "C" AFCORE_EXPORT const afcore_api *afcore_api_gforce2(void) {
#endif
    static const afcore_api api = make_api();
    return &api;
}
