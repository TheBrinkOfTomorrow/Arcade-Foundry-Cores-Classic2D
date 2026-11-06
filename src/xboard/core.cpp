// The X Board cores as afcore libraries (common/include/afcore.h): one game per
// library (After Burner II; After Burner with C2D_ABURNER131; Thunder Blade
// with C2D_THNDRBLD or C2D_THNDRBLD1), driven a frame at a time by the front end.
#include "afcore.h"

#include "xboard/board.h"
#include "xboard/rom_import.h"

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
    std::unique_ptr<xbd::Board> board;
    std::string save_dir, error;
    std::vector<uint32_t> screen = std::vector<uint32_t>(xbd::Video::kWidth * xbd::Video::kHeight, 0xff000000u);
    std::vector<float> audio[2];
    size_t audio_pos[2]{};
};

namespace {

#if defined(C2D_THNDRBLD)
constexpr xbd::Game kGame = xbd::Game::thndrbld;
constexpr const char *kSet = xbd::kThndrbldRomSet, *kTitle = "Thunder Blade";
#elif defined(C2D_THNDRBLD1)
constexpr xbd::Game kGame = xbd::Game::thndrbld1;
constexpr const char *kSet = xbd::kThndrbld1RomSet, *kTitle = "Thunder Blade";
#elif defined(C2D_RACHERO)
constexpr xbd::Game kGame = xbd::Game::rachero;
constexpr const char *kSet = xbd::kRacheroRomSet, *kTitle = "Racing Hero";
#elif defined(C2D_GPRIDER)
constexpr xbd::Game kGame = xbd::Game::gprider;
constexpr const char *kSet = xbd::kGpriderRomSet, *kTitle = "GP Rider";
#elif defined(C2D_LOFFIRE)
constexpr xbd::Game kGame = xbd::Game::loffire;
constexpr const char *kSet = xbd::kLoffireRomSet, *kTitle = "Line of Fire";
#elif defined(C2D_SMGP)
constexpr xbd::Game kGame = xbd::Game::smgp;
constexpr const char *kSet = xbd::kSmgpRomSet, *kTitle = "Super Monaco GP";
#elif defined(C2D_ABURNER131)
constexpr xbd::Game kGame = xbd::Game::aburner131;
constexpr const char *kSet = xbd::kAburner131RomSet, *kTitle = "After Burner";
#else
constexpr xbd::Game kGame = xbd::Game::aburner2;
constexpr const char *kSet = xbd::kRomSet, *kTitle = "After Burner II";
#endif

constexpr float kYmGain = 0.15f, kPcmGain = 0.35f; // segaxbd.cpp's speaker routes

std::string nv_path(const afcore *c) { return c->save_dir + "/" + kSet + ".nv"; }

void copy_error(char *err, size_t n, const std::string &s) {
    if (err && n) std::snprintf(err, n, "%s", s.c_str());
}

// afcore 5.2: the loader create uses reads every file and checks its size
// and CRC; nothing is kept.
int check_roms(const char *rom_path, char *report, size_t report_size) {
    try {
        xbd::import_rom_set(rom_path ? rom_path : "", kGame);
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
        c->board = std::make_unique<xbd::Board>(xbd::import_rom_set(rom_path ? rom_path : "", kGame));
        if (save_dir) c->save_dir = save_dir;
        // backup1 and backup2 are the board's NVRAM (scores, settings): all 0 the first time
        if (!c->save_dir.empty()) {
            std::ifstream in(nv_path(c.get()), std::ios::binary);
            std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), {});
            const size_t n = c->board->backup1.size();
            if (d.size() == 2 * n) {
                std::copy(d.begin(), d.begin() + std::ptrdiff_t(n), c->board->backup1.begin());
                std::copy(d.begin() + std::ptrdiff_t(n), d.end(), c->board->backup2.begin());
            }
        }
        c->board->sound().keep_audio = true;
        if (c->board->sound2()) c->board->sound2()->keep_audio = true;
        return c.release();
    } catch (const std::exception &e) {
        copy_error(err, err_size, e.what());
        return nullptr;
    }
}

void save(afcore *c) {
    if (!c || c->save_dir.empty() || !c->board) return;
    std::ofstream out(nv_path(c), std::ios::binary);
    out.write(reinterpret_cast<const char *>(c->board->backup1.data()), std::streamsize(c->board->backup1.size()));
    out.write(reinterpret_cast<const char *>(c->board->backup2.data()), std::streamsize(c->board->backup2.size()));
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
    xbd::Inputs &p = c->board->in;
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
        if (has(offsetof(afcore_input, gun_y) + sizeof(in->gun_y)))
            for (int i = 0; i < 2; ++i) {
                gun_x[i] = in->gun_x[i];
                gun_y[i] = in->gun_y[i];
            }
        if (has(offsetof(afcore_input, buttons2) + sizeof(uint32_t))) buttons2 = in->buttons2;
        if (has(offsetof(afcore_input, stick_y) + sizeof(float))) stick_y = in->stick_y;
    }
    // IO1 port A: D1 test, D2 service, D3 start, D4 vulcan / cannon, D5 missile, D6 coin 1 (active low)
    uint8_t a = 0xff;
    if (buttons & AFCORE_TEST) a &= ~0x02;
    if (buttons & AFCORE_SERVICE) a &= ~0x04;
    if (buttons & AFCORE_START) a &= ~0x08;
    if (buttons & AFCORE_TRIGGER1) a &= ~0x10;
    if (buttons & AFCORE_BUTTON2) a &= ~0x20;
    if (buttons & AFCORE_COIN) a &= ~0x40;
    p.io1_porta = a;
    if (kGame == xbd::Game::rachero) {
        // Racing Hero: ADC0 the handlebar (PORT_REVERSE) 0xe0 left .. 0x20
        // right, ADC1 the throttle, ADC2 the brake (0 released); D4 and D5
        // (MAME: "move to center", "suicide") are cabinet switches the game
        // never needs pressed, so they stay open (the front ends' fire
        // button would otherwise press D4 with the throttle)
        p.io1_porta = uint8_t(a | 0x30);
        p.adc[0] = analog((1.0f - std::clamp(steer, -1.0f, 1.0f)) * 0.5f, 0x20, 0xe0);
        p.adc[1] = analog(accel, 0, 0xff);
        p.adc[2] = analog(brake, 0, 0xff);
        return;
    }
    if (kGame == xbd::Game::loffire) {
        // Line of Fire's two gun turrets: ADC0/1 player 1's X and Y, ADC2/3
        // player 2's, 0 .. 0xff across and down the screen (the board
        // reverses Y); IO1 port B D7/D6 player 1's trigger and missile
        // button, D5/D4 player 2's. Port A D3 is SERVICE1 here (no start
        // button: a trigger starts), D2 SERVICE2. The front ends' gun
        // convention: TRIGGER1 player 1's trigger, TRIGGER2 player 2's,
        // BUTTON3 the gun's button (player 1's in buttons, player 2's in
        // buttons2), VIEW3 / BUTTON2 player 1's missile too. A turret can't
        // point off screen, so an off-screen aim (the front ends' reload)
        // keeps the last one.
        static float aim[2][2] = {{0.5f, 0.5f}, {0.5f, 0.5f}};
        p.io1_porta = uint8_t(a | 0x3c);
        if (buttons & AFCORE_SERVICE) p.io1_porta &= ~0x08;
        for (int i = 0; i < 2; ++i) {
            if (gun_x[i] >= 0.0f && gun_x[i] <= 1.0f && gun_y[i] >= 0.0f && gun_y[i] <= 1.0f)
                aim[i][0] = gun_x[i], aim[i][1] = gun_y[i];
            p.adc[2 * i] = analog(aim[i][0], 0, 0xff);
            p.adc[2 * i + 1] = analog(aim[i][1], 0, 0xff);
        }
        uint8_t b = 0xff;
        if (buttons & (AFCORE_TRIGGER1 | AFCORE_START)) b &= ~0x80;
        if (buttons & (AFCORE_BUTTON3 | AFCORE_BUTTON2 | AFCORE_VIEW3)) b &= ~0x40;
        if ((buttons & (AFCORE_TRIGGER2 | AFCORE_START2)) || (buttons2 & AFCORE_TRIGGER1)) b &= ~0x20;
        if (buttons2 & (AFCORE_BUTTON3 | AFCORE_BUTTON2)) b &= ~0x10;
        p.io1_portb = b;
        return;
    }
    if (kGame == xbd::Game::gprider) {
        // GP Rider: ADC0 the handlebar 0x01 left .. 0xff right, ADC1 the
        // throttle and ADC2 the brake 0xef released .. 0x10 full (MAME's
        // PORT_REVERSE pedals); the shift switches are D4 (down) and D5
        // (up), held while pressed
        a |= 0x30;
        if (buttons & AFCORE_SHIFT_DOWN) a &= ~0x10;
        if (buttons & AFCORE_SHIFT_UP) a &= ~0x20;
        p.io1_porta = a;
        p.adc[0] = analog((std::clamp(steer, -1.0f, 1.0f) + 1.0f) * 0.5f, 0x01, 0xff);
        p.adc[1] = analog(1.0f - accel, 0x10, 0xef);
        p.adc[2] = analog(1.0f - brake, 0x10, 0xef);
        return;
    }
    if (kGame == xbd::Game::smgp) {
        // Super Monaco GP: ADC0 the wheel 0x38 left .. 0xc8 right, ADC1 the
        // gas 0x38 .. 0xb8, ADC2 the brake 0x28 .. 0xa8; the shift lever's
        // two switches are D4 (down) and D5 (up), held while pressed (not
        // TRIGGER1 / BUTTON2: the front ends set TRIGGER1 with the throttle)
        a |= 0x30;
        if (buttons & AFCORE_SHIFT_DOWN) a &= ~0x10;
        if (buttons & AFCORE_SHIFT_UP) a &= ~0x20;
        p.io1_porta = a;
        p.adc[0] = analog((std::clamp(steer, -1.0f, 1.0f) + 1.0f) * 0.5f, 0x38, 0xc8);
        p.adc[1] = analog(accel, 0x38, 0xb8);
        p.adc[2] = analog(brake, 0x28, 0xa8);
        return;
    }
    if (!xbd::is_aburner(kGame)) {
        // Thunder Blade (econ upright / deluxe): ADC0 stick X (PORT_REVERSE)
        // 0xff left .. 0x01 right, ADC1 the throttle ("slottle") 0x80 at
        // rest, ADC2 stick Y 0x01 pushed forward .. 0xff pulled back
        p.adc[0] = analog((1.0f - std::clamp(steer, -1.0f, 1.0f)) * 0.5f, 0x01, 0xff);
        p.adc[1] = analog(std::clamp(0.5f + 0.5f * accel - 0.5f * brake, 0.0f, 1.0f), 0, 0xff);
        p.adc[2] = analog((std::clamp(stick_y, -1.0f, 1.0f) + 1.0f) * 0.5f, 0x01, 0xff);
        return;
    }
    // the stick: X 0x20 (left) .. 0xe0 (right); Y (PORT_REVERSE) 0xc0 pushed
    // forward .. 0x40 pulled back; the throttle lever 0x80 at rest, accel
    // pushes it up to 0xff, brake pulls it back to 0
    p.adc[0] = analog((std::clamp(steer, -1.0f, 1.0f) + 1.0f) * 0.5f, 0x20, 0xe0);
    p.adc[1] = analog((std::clamp(stick_y, -1.0f, 1.0f) + 1.0f) * 0.5f, 0xc0, 0x40);
    const float throttle = std::clamp(0.5f + 0.5f * accel - 0.5f * brake, 0.0f, 1.0f);
    p.adc[2] = analog(throttle, 0, 0xff);
}

void take_audio(afcore *c) {
    xbd::Sound &snd = c->board->sound();
    if (xbd::Sound *s2 = c->board->sound2()) {
        // Super Monaco GP's rear speakers (pcm2, the same clock and gain):
        // mixed into the PCM stream, sample for sample
        std::vector<int32_t> &a = snd.pcm_out, &b = s2->pcm_out;
        const size_t n = std::min(a.size(), b.size());
        for (size_t i = 0; i < n; ++i) a[i] += b[i];
        b.erase(b.begin(), b.begin() + std::ptrdiff_t(n));
        if (b.size() > 2 * 62500) b.clear();
    }
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
    if (xbd::Video *v = c->board->video())
        for (size_t i = 0; i < v->bitmap.size(); ++i) c->screen[i] = 0xff000000u | v->rgb(v->bitmap[i]);
    take_audio(c);
}

const uint32_t *screen(afcore *c, int *w, int *h) {
    if (w) *w = xbd::Video::kWidth;
    if (h) *h = xbd::Video::kHeight;
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
    a.frame_hz = 6250000.0 / (400.0 * 262.0); // set_raw(50 MHz / 8, 400, 0, 320, 262, 0, 224)
    a.width = xbd::Video::kWidth;
    a.height = xbd::Video::kHeight;
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

#if defined(C2D_THNDRBLD)
extern "C" AFCORE_EXPORT const afcore_api *afcore_api_thndrbld(void) {
#elif defined(C2D_THNDRBLD1)
extern "C" AFCORE_EXPORT const afcore_api *afcore_api_thndrbld1(void) {
#elif defined(C2D_RACHERO)
extern "C" AFCORE_EXPORT const afcore_api *afcore_api_rachero(void) {
#elif defined(C2D_LOFFIRE)
extern "C" AFCORE_EXPORT const afcore_api *afcore_api_loffire(void) {
#elif defined(C2D_GPRIDER)
extern "C" AFCORE_EXPORT const afcore_api *afcore_api_gprider(void) {
#elif defined(C2D_SMGP)
extern "C" AFCORE_EXPORT const afcore_api *afcore_api_smgp(void) {
#elif defined(C2D_ABURNER131)
extern "C" AFCORE_EXPORT const afcore_api *afcore_api_aburner131(void) {
#else
extern "C" AFCORE_EXPORT const afcore_api *afcore_api_aburner2(void) {
#endif
    static const afcore_api api = make_api();
    return &api;
}
