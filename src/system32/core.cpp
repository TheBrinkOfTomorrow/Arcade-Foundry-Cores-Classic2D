// OutRunners (System Multi 32) or, with C2D_RADR / C2D_RADM / C2D_F1EN /
// C2D_JPARK / C2D_SONIC / C2D_GA2, Rad Rally / Rad Mobile / F1 Exhaust Note /
// Jurassic Park / SegaSonic the Hedgehog / Golden Axe: The Revenge of Death
// Adder (System 32) as
// an afcore library (common/include/afcore.h), driven a frame at a time by the
// front end. OutRunners: one cabinet, player 1's monitor and speaker;
// player 2's controls stay idle. F1 Exhaust Note: two boards (one per
// seat) in one machine; the front end drives and sees the main board, the
// sub board's seat stays idle.
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
    std::unique_ptr<s32::Board> sub; // F1 Exhaust Note's second board
    std::string save_dir, error;
    std::vector<uint32_t> screen = std::vector<uint32_t>(s32::Video::kWidth * s32::Video::kHeight, 0xff000000u);
    int width = s32::Video::kWidth; // the visible width of the last frame
    std::vector<float> audio[2];
    size_t audio_pos[2]{};
    bool high_gear = false; // Rad Rally's two-position shifter
    float aim[2][2] = {{0.5f, 0.5f}, {0.5f, 0.5f}}; // Jurassic Park: the last on-screen aim
    float track[4] = {};                             // SegaSonic: players 1 and 2's trackball X, Y
};

namespace {

#if defined(C2D_RADR) || defined(C2D_RADM) || defined(C2D_F1EN) || defined(C2D_JPARK) || defined(C2D_SONIC) || defined(C2D_GA2)
#if defined(C2D_GA2)
constexpr const char *kSet = s32::kGa2RomSet, *kTitle = "Golden Axe: The Revenge of Death Adder";
#elif defined(C2D_SONIC)
constexpr const char *kSet = s32::kSonicRomSet, *kTitle = "SegaSonic the Hedgehog";
#elif defined(C2D_JPARK)
constexpr const char *kSet = s32::kJparkRomSet, *kTitle = "Jurassic Park";
#elif defined(C2D_F1EN)
constexpr const char *kSet = s32::kF1enRomSet, *kTitle = "F1 Exhaust Note";
#elif defined(C2D_RADM)
constexpr const char *kSet = s32::kRadmRomSet, *kTitle = "Rad Mobile";
#else
constexpr const char *kSet = s32::kRadrRomSet, *kTitle = "Rad Rally";
#endif
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

// afcore 5.2: the loader create uses reads every file and checks its size
// and CRC; nothing is kept.
int check_roms(const char *rom_path, char *report, size_t report_size) {
    try {
        s32::import_rom_set(rom_path ? rom_path : "", kSet);
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
        s32::Roms roms = s32::import_rom_set(rom_path ? rom_path : "", kSet);
#if defined(C2D_F1EN)
        c->board = std::make_unique<s32::Board>(roms, v60n::run_native);
        c->sub = std::make_unique<s32::Board>(std::move(roms), v60n::run_native, &c->board->scheduler());
        auto link = std::make_shared<s32::DualLink>();
        c->board->attach_dual(link, 0);
        c->sub->attach_dual(link, 1);
#else
        c->board = std::make_unique<s32::Board>(std::move(roms), v60n::run_native);
#endif
        if (save_dir) c->save_dir = save_dir;
        // the 93C46 (settings, records): the set's image (Rad Rally) or
        // erased (0xff) the first time; F1 Exhaust Note: the main board's,
        // then the sub board's
        if (!c->save_dir.empty()) {
            std::ifstream in(nv_path(c.get()), std::ios::binary);
            std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), {});
            const size_t n = c->board->eeprom.size();
            if (d.size() == n || (c->sub && d.size() == 2 * n)) std::copy(d.begin(), d.begin() + std::ptrdiff_t(n), c->board->eeprom.begin());
            if (c->sub && d.size() == 2 * n) std::copy(d.begin() + std::ptrdiff_t(n), d.end(), c->sub->eeprom.begin());
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
    if (c->sub) {
        out.write(reinterpret_cast<const char *>(c->sub->eeprom.data()), std::streamsize(c->sub->eeprom.size()));
        c->sub->eeprom_dirty = false;
    }
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
    uint32_t buttons = 0, buttons2 = 0;
    float steer = 0, accel = 0, brake = 0;
    float gun_x[2] = {-1.0f, -1.0f}, gun_y[2] = {-1.0f, -1.0f};
    float stick_y = 0;
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
    uint8_t a = 0xff, b = 0xff, s = 0xff;
#if defined(C2D_GA2)
    // P1_A / P2_A: D0-D2 buttons 1-3 (PUNCH, KICK, GUARD; player 2's in
    // buttons2), D4 down, D5 up, D6 right, D7 left; start 2 on START2 or
    // buttons2's START. Players 3 and 4 (EXTRA1-3) stay idle.
    (void)gun_x, (void)gun_y, (void)accel, (void)brake, (void)stick_y;
    for (int i = 0; i < 2; ++i) {
        const uint32_t bt = i ? buttons2 : buttons;
        uint8_t &port = i ? b : a;
        if (bt & AFCORE_PUNCH) port &= ~0x01;
        if (bt & AFCORE_KICK) port &= ~0x02;
        if (bt & AFCORE_GUARD) port &= ~0x04;
        if (bt & AFCORE_DOWN) port &= ~0x10;
        if (bt & AFCORE_UP) port &= ~0x20;
        if (bt & AFCORE_RIGHT) port &= ~0x40;
        if (bt & AFCORE_LEFT) port &= ~0x80;
    }
    if (!(buttons & (AFCORE_LEFT | AFCORE_RIGHT))) {
        if (steer < -0.5f) a &= ~0x80;
        if (steer > 0.5f) a &= ~0x40;
    }
    if ((buttons & AFCORE_START2) || (buttons2 & AFCORE_START)) s &= ~0x20; // start 2
#elif defined(C2D_SONIC)
    // the trackballs (TRACKX1/Y1, X2/Y2; MAME reverses X: right counts
    // down) roll with the stick or the directions, 24 counts a frame at
    // full tilt; P1_A / P2_A D0 the buttons (PUNCH, KICK or BUTTON2;
    // player 2's in buttons2), start 2 on START2 or buttons2's START
    (void)gun_x, (void)gun_y, (void)accel, (void)brake;
    constexpr float kRoll = 24.0f;
    for (int i = 0; i < 2; ++i) {
        const uint32_t bt = i ? buttons2 : buttons;
        float dx = i ? 0.0f : steer, dy = i ? 0.0f : stick_y;
        if (bt & AFCORE_LEFT) dx = -1.0f;
        if (bt & AFCORE_RIGHT) dx = 1.0f;
        if (bt & AFCORE_UP) dy = -1.0f;
        if (bt & AFCORE_DOWN) dy = 1.0f;
        c->track[2 * i] = std::fmod(c->track[2 * i] - kRoll * std::clamp(dx, -1.0f, 1.0f) + 4096.0f, 4096.0f);
        c->track[2 * i + 1] = std::fmod(c->track[2 * i + 1] + kRoll * std::clamp(dy, -1.0f, 1.0f) + 4096.0f, 4096.0f);
        p.track[2 * i] = uint16_t(std::lround(c->track[2 * i])) & 0xfff;
        p.track[2 * i + 1] = uint16_t(std::lround(c->track[2 * i + 1])) & 0xfff;
        if (bt & (AFCORE_PUNCH | AFCORE_KICK | AFCORE_BUTTON2)) (i ? b : a) &= ~0x01;
    }
    if ((buttons & AFCORE_START2) || (buttons2 & AFCORE_START)) s &= ~0x20; // start 2
#elif defined(C2D_JPARK)
    // the two guns: P1_A / P2_A D0 the triggers (TRIGGER1 or START in
    // buttons, buttons2's TRIGGER1 for player 2), ANALOG1/2 and 3/4 the aim
    // (0x00-0xff across and down; the game calibrates in test mode). An
    // off-screen aim (the front ends' reload) keeps the last one.
    if (buttons & AFCORE_TRIGGER1) a &= ~0x01;
    if ((buttons & AFCORE_TRIGGER2) || (buttons2 & AFCORE_TRIGGER1)) b &= ~0x01;
    if ((buttons & AFCORE_START2) || (buttons2 & AFCORE_START)) s &= ~0x20; // start 2
    for (int i = 0; i < 2; ++i)
        if (gun_x[i] >= 0.0f && gun_x[i] <= 1.0f && gun_y[i] >= 0.0f && gun_y[i] <= 1.0f) {
            c->aim[i][0] = gun_x[i];
            c->aim[i][1] = gun_y[i];
        }
#elif defined(C2D_F1EN)
    // P1_A D0 gear up, D1 gear down
    if (buttons & AFCORE_SHIFT_UP) a &= ~0x01;
    if (buttons & AFCORE_SHIFT_DOWN) a &= ~0x02;
#elif defined(C2D_RADM)
    // P1_A D1 the light switch, D2 the wiper (the front ends' A and B view
    // buttons, or BUTTON2 / BUTTON3)
    if (buttons & (AFCORE_VIEW1 | AFCORE_BUTTON2)) a &= ~0x02;
    if (buttons & (AFCORE_VIEW2 | AFCORE_BUTTON3)) a &= ~0x04;
#elif defined(C2D_RADR)
    // P1_A D0: the gear lever (a toggle in MAME); the shift buttons move it
    // to high or low
    if (buttons & AFCORE_SHIFT_UP) c->high_gear = true;
    if (buttons & AFCORE_SHIFT_DOWN) c->high_gear = false;
    if (c->high_gear) a &= ~0x01;
#else
    // P1_A: D0 shift up, D1 shift down; P2_A: D0 DJ/music, D1 <<, D2 >>
    // (the music buttons on the front ends' view buttons: A, B, X)
    if (buttons & AFCORE_SHIFT_UP) a &= ~0x01;
    if (buttons & AFCORE_SHIFT_DOWN) a &= ~0x02;
    if (buttons & AFCORE_VIEW1) b &= ~0x01;
    if (buttons & (AFCORE_BUTTON2 | AFCORE_VIEW2)) b &= ~0x02;
    if (buttons & (AFCORE_BUTTON3 | AFCORE_VIEW3)) b &= ~0x04;
#endif
    // SERVICE12_A: D0 service, D1 test, D2 coin 1, D4 start 1
    if (buttons & AFCORE_SERVICE) s &= ~0x01;
    if (buttons & AFCORE_TEST) s &= ~0x02;
    if (buttons & AFCORE_COIN) s &= ~0x04;
    if (buttons & AFCORE_START) s &= ~0x10;
    p.p1_a = a;
    p.p2_a = b;
    p.service12_a = s;
#if defined(C2D_SONIC) || defined(C2D_GA2)
    (void)steer;
#elif defined(C2D_JPARK)
    (void)steer, (void)accel, (void)brake;
    for (int i = 0; i < 2; ++i) {
        p.analog[2 * i] = analog(c->aim[i][0], 0x00, 0xff);
        p.analog[2 * i + 1] = analog(c->aim[i][1], 0x00, 0xff);
    }
#else
    // ANALOG1 the wheel (a paddle, 0x80 centred), ANALOG2 gas, ANALOG3 brake
    p.analog[0] = analog((std::clamp(steer, -1.0f, 1.0f) + 1.0f) * 0.5f, 0x00, 0xff);
    p.analog[1] = analog(accel, 0, 0xff);
    p.analog[2] = analog(brake, 0, 0xff);
#endif
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
    // the visible width the game set (416, or 320 when 0x1ff00 bit 15 is
    // clear, as OutRunners runs), as MAME's screen shows it
    const std::vector<uint32_t> &s = c->board->screen[0];
    const int w = std::clamp(c->board->video().width(), 1, s32::Video::kWidth);
    c->width = w;
    for (int y = 0; y < s32::Video::kHeight; ++y)
        for (int x = 0; x < w; ++x) c->screen[size_t(y) * w + x] = 0xff000000u | s[size_t(y) * s32::Video::kWidth + x];
    take_audio(c);
}

const uint32_t *screen(afcore *c, int *w, int *h) {
    if (w) *w = c ? c->width : s32::Video::kWidth;
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

#if defined(C2D_GA2)
extern "C" AFCORE_EXPORT const afcore_api *afcore_api_ga2(void) {
#elif defined(C2D_SONIC)
extern "C" AFCORE_EXPORT const afcore_api *afcore_api_sonic(void) {
#elif defined(C2D_JPARK)
extern "C" AFCORE_EXPORT const afcore_api *afcore_api_jpark(void) {
#elif defined(C2D_F1EN)
extern "C" AFCORE_EXPORT const afcore_api *afcore_api_f1en(void) {
#elif defined(C2D_RADM)
extern "C" AFCORE_EXPORT const afcore_api *afcore_api_radm(void) {
#elif defined(C2D_RADR)
extern "C" AFCORE_EXPORT const afcore_api *afcore_api_radr(void) {
#else
extern "C" AFCORE_EXPORT const afcore_api *afcore_api_orunners(void) {
#endif
    static const afcore_api api = make_api();
    return &api;
}
