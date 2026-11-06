// license:BSD-3-Clause
// copyright-holders:Aaron Giles, Charles MacDonald
//
// MAME's segaybd.cpp screen_update, sega16sp.cpp (Y Board and System 16B
// sprites), segaic16.cpp (rotate_draw, the palette) and resnet.cpp's
// weights, transplanted (see video.h).
#include "yboard/video.h"

#include <algorithm>
#include <cstring>

namespace ybd {

namespace {

inline uint16_t be16(const uint8_t *a, size_t word) { return uint16_t(a[word * 2] << 8 | a[word * 2 + 1]); }
inline void set_be16(uint8_t *a, size_t word, uint16_t v) {
    a[word * 2] = uint8_t(v >> 8);
    a[word * 2 + 1] = uint8_t(v);
}

// resnet.cpp compute_resistor_weights for one network, autoscaled to 0-255
void resistor_weights(const int (&res)[6], double (&out)[6]) {
    const int count = 6;
    double w[6];
    for (int n = 0; n < count; ++n) {
        double R0 = 1.0 / 1e12, R1 = 1.0 / 1e12;
        for (int j = 0; j < count; ++j) {
            if (j == n) {
                if (res[j] != 0) R1 += 1.0 / double(res[j]);
            } else if (res[j] != 0)
                R0 += 1.0 / double(res[j]);
        }
        R0 = 1.0 / R0;
        R1 = 1.0 / R1;
        const double vout = (255 - 0) * R0 / (R1 + R0) + 0;
        w[n] = std::clamp(vout, 0.0, 255.0);
    }
    double sum = 0.0;
    for (int n = 0; n < count; ++n) sum += w[n];
    const double scale = 255.0 / sum;
    for (int n = 0; n < count; ++n) out[n] = w[n] * scale;
}

uint8_t combine(const double (&t)[6], int w0, int w1, int w2, int w3, int w4, int w5) {
    const double v = t[0] * w0 + (t[1] * w1 + (t[2] * w2 + (t[3] * w3 + (t[4] * w4 + t[5] * w5))));
    return uint8_t(int(v + 0.5));
}

constexpr int kYSize = 512, kYOrigin = 0x600;

} // namespace

Video::Video(VideoRoms roms, VideoView view) : v_(view) {
    if (roms.ysprites.size() < 0x400000) roms.ysprites.resize(0x400000, 0);
    yrom_.resize(roms.ysprites.size() / 8);
    for (size_t i = 0; i < yrom_.size(); ++i) {
        uint64_t w = 0;
        for (int b = 0; b < 8; ++b) w = w << 8 | roms.ysprites[i * 8 + size_t(b)];
        yrom_[i] = w;
    }
    if (roms.bsprites.size() < 0x80000) roms.bsprites.resize(0x80000, 0);
    brom_.resize(roms.bsprites.size() / 2);
    for (size_t i = 0; i < brom_.size(); ++i) brom_[i] = be16(roms.bsprites.data(), i);
    ybitmap_.assign(size_t(kYSize) * kYSize, 0xffff);
    bbitmap_.assign(size_t(kWidth) * kHeight, 0xffff);
    priority_.assign(size_t(kWidth) * kHeight, 0);

    // sega_16bit_common_base::palette_init
    static const int normal[6] = {3900, 2000, 1000, 1000 / 2, 1000 / 4, 0};
    static const int sh[6] = {3900, 2000, 1000, 1000 / 2, 1000 / 4, 470};
    double wn[6], ws[6];
    resistor_weights(normal, wn);
    resistor_weights(sh, ws);
    for (int v = 0; v < 32; ++v) {
        const int i4 = (v >> 4) & 1, i3 = (v >> 3) & 1, i2 = (v >> 2) & 1, i1 = (v >> 1) & 1, i0 = v & 1;
        pal_normal_[v] = combine(wn, i0, i1, i2, i3, i4, 0);
        pal_shadow_[v] = combine(ws, i0, i1, i2, i3, i4, 0);
        pal_hilight_[v] = combine(ws, i0, i1, i2, i3, i4, 1);
    }
}

uint32_t Video::rgb(uint16_t index) const {
    if (index >= 0x4000) return index == 0x4001 ? 0xffffffu : 0; // black pen, white pen
    // device_palette_interface::allocate_palette: a standard rainbow until
    // paletteram_w first sets the pen
    if (!v_.pal_written[index & 0x1fff])
        return (index & 1 ? 0xff0000u : 0) | (index & 2 ? 0x00ff00u : 0) | (index & 4 ? 0x0000ffu : 0);
    const uint16_t v = be16(v_.palette, index & 0x1fff);
    const int r = ((v >> 12) & 1) | ((v << 1) & 0x1e), g = ((v >> 13) & 1) | ((v >> 3) & 0x1e), bl = ((v >> 14) & 1) | ((v >> 7) & 0x1e);
    const uint8_t *t = index < 0x2000 ? pal_normal_ : (v & 0x8000) ? pal_hilight_ : pal_shadow_;
    return uint32_t(t[r]) << 16 | uint32_t(t[g]) << 8 | t[bl];
}

// sega_yboard_sprite_device::draw over the whole 512x512 bitmap
// (cliprect 0x600-0x7ff both ways in sprite coordinates)
void Video::draw_ysprites() {
    std::fill(ybitmap_.begin(), ybitmap_.end(), uint16_t(0xffff));
    const int cmin = kYOrigin, cmax = kYOrigin + kYSize - 1;
    uint16_t ram[0x8000];
    for (size_t i = 0; i < 0x8000; ++i) ram[i] = be16(v_.ysprites, i);
    uint16_t rot[0x400];
    for (size_t i = 0; i < 0x400; ++i) rot[i] = be16(v_.rotate_buffer, i);
    auto rotatebase = [&](int y) { return rot[(y - kYOrigin) & 0x3ff]; };

    uint8_t visited[0x1000];
    std::memset(visited, 0, sizeof visited);
    const uint64_t numbanks = yrom_.size() / 0x10000;
    int next = 0;
    for (const uint16_t *data = ram; !(data[0] & 0x8000) && !visited[next]; data = ram + next * 8) {
        const int hide = data[0] & 0x5000;
        const uint16_t *indirect = ram + ((data[0] & 0x7ff) << 4);
        int bank = ((data[1] >> 8) & 0x10) | ((data[2] >> 12) & 0x0f);
        const int xpos = data[1] & 0xfff;
        const int top = data[2] & 0xfff;
        uint16_t addr = data[3];
        const int height = data[4];
        const int ydelta = (data[5] & 0x4000) ? 1 : -1;
        const int flip = (~data[5] >> 13) & 1;
        const int xdelta = (data[5] & 0x1000) ? 1 : -1;
        int zoom = data[5] & 0x7ff;
        const int colpri = (data[6] << 1) & 0xfe00;
        const int pitch = int8_t(data[6]);
        visited[next] = 1;
        next = data[7] & 0xfff;
        if (hide || height == 0) continue;
        if (numbanks) bank = int(uint64_t(bank) % numbanks);
        const uint64_t *spritedata = yrom_.data() + 0x10000 * size_t(bank);
        if (zoom == 0) zoom = 1;
        const int ytarget = top + ydelta * height;
        int yacc = 0;
        for (int y = top; y != ytarget; y += ydelta) {
            if (y >= cmin && y <= cmax) {
                uint16_t *dest = &ybitmap_[size_t(y - kYOrigin) * kYSize] - kYOrigin;
                int minx = rotatebase(y & ~1);
                int maxx = rotatebase(y | 1);
                int xacc = 0;
                if ((minx & 0x8000) && ydelta < 0) break;
                if ((minx & 0x4000) && ydelta > 0) break;
                if (!(minx & 0xc000)) {
                    if (minx < cmin) minx = cmin;
                    if (maxx > cmax) maxx = cmax;
                    int x = xpos;
                    auto plot = [&](int pix) {
                        const int ind = indirect[pix];
                        while (xacc < 0x200) {
                            if (x >= minx && x <= maxx && ind < 0x1fe) dest[x] = uint16_t(colpri | ind);
                            x += xdelta;
                            xacc += zoom;
                        }
                        xacc -= 0x200;
                    };
                    auto more = [&] { return (xdelta > 0 && x <= maxx) || (xdelta < 0 && x >= minx); };
                    if (!flip) {
                        uint16_t offs = uint16_t(addr - 1);
                        while (more()) {
                            const uint64_t pixels = spritedata[++offs];
                            int pix = 0;
                            for (int sh = 60; sh >= 0; sh -= 4) plot(pix = int((pixels >> sh) & 0xf));
                            if (pix == 0x0f) break;
                        }
                    } else {
                        uint16_t offs = uint16_t(addr + 1);
                        while (more()) {
                            const uint64_t pixels = spritedata[--offs];
                            int pix = 0;
                            for (int sh = 0; sh <= 60; sh += 4) plot(pix = int((pixels >> sh) & 0xf));
                            if (pix == 0x0f) break;
                        }
                    }
                }
            }
            yacc += zoom;
            addr = uint16_t(addr + pitch * (yacc >> 9));
            yacc &= 0x1ff;
        }
    }
}

// sega_sys16b_sprite_device::draw (origin 184, no flip, m_bank identity)
void Video::draw_bsprites(int min_y, int max_y) {
    for (int y = min_y; y <= max_y; ++y) std::fill_n(&bbitmap_[size_t(y) * kWidth], kWidth, uint16_t(0xffff));
    const int origin = 184;
    const int cmin_x = origin, cmax_x = origin + kWidth - 1;
    const int numbanks = int(brom_.size() / 0x10000);
    uint8_t *ram = v_.bsprites;
    for (size_t s = 0; s < 0x1000 / 16; ++s) {
        const size_t w0 = s * 8;
        auto data = [&](int i) { return be16(ram, w0 + size_t(i)); };
        if (data(2) & 0x8000) break;
        const int bottom = data(0) >> 8, top = data(0) & 0xff;
        const int xpos = data(1) & 0x1ff;
        const int hide = data(2) & 0x4000, flip = data(2) & 0x100;
        const int pitch = int8_t(data(2) & 0xff);
        uint16_t addr = data(3);
        int bank = (data(4) >> 8) & 0xf;
        const int colpri = ((data(4) & 0xff) << 4) | (((data(1) >> 9) & 0xf) << 12);
        const int vzoom = (data(5) >> 5) & 0x1f, hzoom = data(5) & 0x1f;
        set_be16(ram, w0 + 7, addr);
        if (hide || top >= bottom) continue;
        if (numbanks) bank %= numbanks;
        const uint16_t *spritedata = brom_.data() + 0x10000 * size_t(bank);
        uint16_t d5 = uint16_t(data(5) & 0x03ff);
        set_be16(ram, w0 + 5, d5);
        for (int y = top; y < bottom; ++y) {
            addr = uint16_t(addr + pitch);
            d5 = uint16_t(d5 + (vzoom << 10));
            if (d5 & 0x8000) {
                addr = uint16_t(addr + pitch);
                d5 &= ~0x8000;
            }
            set_be16(ram, w0 + 5, d5);
            if (y < min_y || y > max_y) continue;
            uint16_t *dest = &bbitmap_[size_t(y) * kWidth] - origin;
            int xacc = 4 * hzoom;
            int x = xpos;
            auto plot = [&](int pix) {
                xacc = (xacc & 0x3f) + hzoom;
                if (xacc < 0x40) {
                    if (x >= cmin_x && x <= cmax_x && pix != 0 && pix != 15) dest[x] = uint16_t(colpri | pix);
                    ++x;
                }
            };
            if (!flip) {
                uint16_t scratch = uint16_t(addr - 1);
                for (; ((xpos - x) & 0x1ff) != 1;) {
                    const uint16_t pixels = spritedata[++scratch];
                    int pix = 0;
                    for (int sh = 12; sh >= 0; sh -= 4) plot(pix = (pixels >> sh) & 0xf);
                    if (pix == 15) break;
                }
                set_be16(ram, w0 + 7, scratch);
            } else {
                uint16_t scratch = uint16_t(addr + 1);
                for (; ((xpos - x) & 0x1ff) != 1;) {
                    const uint16_t pixels = spritedata[--scratch];
                    int pix = 0;
                    for (int sh = 0; sh <= 12; sh += 4) plot(pix = (pixels >> sh) & 0xf);
                    if (pix == 15) break;
                }
                set_be16(ram, w0 + 7, scratch);
            }
        }
    }
}

// segaic16_video_device::rotate_draw (colorbase 0)
void Video::rotate_draw(int min_y, int max_y) {
    auto word = [&](size_t i) { return uint32_t(be16(v_.rotate_buffer, i)); };
    uint32_t currx = word(0x3f0) << 16 | word(0x3f1);
    uint32_t curry = word(0x3f2) << 16 | word(0x3f3);
    const uint32_t dyy = word(0x3f4) << 16 | word(0x3f5);
    const uint32_t dxx = word(0x3f6) << 16 | word(0x3f7);
    const uint32_t dxy = word(0x3f8) << 16 | word(0x3f9);
    const uint32_t dyx = word(0x3fa) << 16 | word(0x3fb);
    const int min_x = 0, max_x = kWidth - 1;
    currx += dxx * uint32_t(min_x + 27) + dxy * uint32_t(min_y);
    curry += dyx * uint32_t(min_x + 27) + dyy * uint32_t(min_y);
    for (int y = min_y; y <= max_y; ++y) {
        uint16_t *dest = &bitmap[size_t(y) * kWidth];
        uint8_t *pri = &priority_[size_t(y) * kWidth];
        uint32_t tx = currx, ty = curry;
        for (int x = min_x; x <= max_x; ++x) {
            const int sx = int(tx >> 14) & 0x1ff, sy = int(ty >> 14) & 0x1ff;
            const uint16_t pix = ybitmap_[size_t(sy) * kYSize + size_t(sx)];
            if (pix != 0xffff) {
                dest[x] = uint16_t((pix & 0x1ff) | ((pix >> 6) & 0x200) | ((pix >> 3) & 0xc00) | 0x1000);
                pri[x] = uint8_t((pix >> 8) | 1);
            } else {
                dest[x] = uint16_t(sy);
                pri[x] = 0xff;
            }
            tx += dxx;
            ty += dyx;
        }
        currx += dxy;
        curry += dyy;
    }
}

void Video::update(int min_y, int max_y) {
    if (!*v_.display_enable) {
        for (int y = min_y; y <= max_y; ++y) std::fill_n(&bitmap[size_t(y) * kWidth], kWidth, kBlackPen);
        return;
    }
    draw_ysprites();
    draw_bsprites(min_y, max_y);
    rotate_draw(min_y, max_y);
    const uint16_t entries = *v_.palette_entries;
    for (int y = min_y; y <= max_y; ++y) {
        uint16_t *dest = &bitmap[size_t(y) * kWidth];
        const uint16_t *src = &bbitmap_[size_t(y) * kWidth];
        const uint8_t *pri = &priority_[size_t(y) * kWidth];
        for (int x = 0; x < kWidth; ++x) {
            const uint16_t pix = src[x];
            if (pix == 0xffff) continue;
            const int priority = (pix >> 11) & 0x1e;
            if (priority < (pri[x] & 0x1f)) {
                if ((pix & 0xf) == 0xe) dest[x] = uint16_t(dest[x] + entries);
                else dest[x] = uint16_t(0x800 | (pix & 0x7ff));
            }
        }
    }
}

} // namespace ybd
