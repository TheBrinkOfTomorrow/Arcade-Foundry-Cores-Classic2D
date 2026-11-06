// license:BSD-3-Clause
// copyright-holders:Aaron Giles, Charles MacDonald
//
// MAME's segaic16.cpp (16B tilemaps, palette), sega16sp.cpp (Out Run and
// X Board sprites), segaic16_road.cpp (Out Run road), tilemap.cpp's drawing
// rules and the screen updates of segaorun.cpp and segaxbd.cpp,
// transplanted (see video.h).
#include "outrun/video.h"

#include <algorithm>
#include <cstring>

namespace orun {

namespace {

// big-endian word access to the board's RAM arrays
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

// resnet.h combine_weights: tab[0]*w0 + (tab[1]*w1 + (... + tab[5]*w5))
uint8_t combine(const double (&t)[6], int w0, int w1, int w2, int w3, int w4, int w5) {
    const double v = t[0] * w0 + (t[1] * w1 + (t[2] * w2 + (t[3] * w3 + (t[4] * w4 + t[5] * w5))));
    return uint8_t(int(v + 0.5));
}

} // namespace

Video::Video(VideoRoms roms, VideoConfig cfg, VideoView view) : cfg_(cfg), v_(view) {
    // gfx_8x8x3_planar over gfx1: planes at 2/3, 1/3, 0/3 of the region,
    // most significant first; pixel 0 is bit 7
    if (roms.tiles.empty()) roms.tiles.resize(3 * 8, 0);
    const size_t plane = roms.tiles.size() / 3;
    tiles_ = uint32_t(plane / 8);
    tile_pens_.resize(size_t(tiles_) * 64);
    for (uint32_t t = 0; t < tiles_; ++t)
        for (int y = 0; y < 8; ++y) {
            const uint8_t p2 = roms.tiles[2 * plane + t * 8 + y], p1 = roms.tiles[plane + t * 8 + y], p0 = roms.tiles[t * 8 + y];
            for (int x = 0; x < 8; ++x) {
                const int b = 7 - x;
                tile_pens_[t * 64 + y * 8 + x] = uint8_t(((p2 >> b) & 1) << 2 | ((p1 >> b) & 1) << 1 | ((p0 >> b) & 1));
            }
        }

    if (cfg_.hangon) {
        zoom_ = std::move(roms.zoom);
        zoom_.resize(0x2000, 0);
        // segaic16_road_hangon_decode
        const int hlen = int(roms.road.size());
        hangon_road_gfx_.assign(256 * 512, 0);
        for (int y = 0; y < 256 && hlen; ++y) {
            const uint8_t *src = roms.road.data() + ((y & 0xff) * 0x40) % hlen;
            uint8_t *dst = hangon_road_gfx_.data() + y * 512;
            for (int x = 0; x < 512; ++x)
                dst[x] = uint8_t(((src[x >> 3] >> (~x & 7)) & 1) << 0 | ((src[(x >> 3) + 0x4000] >> (~x & 7)) & 1) << 1);
        }
    }
    if (cfg_.sharrier) {
        // ROM_REGION32_LE sprites: 0x8000 words per bank
        hangon_sprite_banks_ = int(roms.sprites.size() / 0x20000);
    } else if (cfg_.hangon) {
        // ROM_REGION16_BE sprites: 0x8000 words per bank
        hangon_sprite_banks_ = int(roms.sprites.size() / 0x10000);
        sprite_rom16_.resize(roms.sprites.size() / 2);
        for (size_t i = 0; i < sprite_rom16_.size(); ++i) sprite_rom16_[i] = uint16_t(roms.sprites[i * 2] << 8 | roms.sprites[i * 2 + 1]);
        roms.sprites.clear();
    }
    roms.sprites.resize(cfg_.xboard ? 0x200000 : 0x100000, 0);
    if (cfg_.sys16b_sprites) {
        sprite_rom16_.resize(roms.sprites.size() / 2);
        for (size_t i = 0; i < sprite_rom16_.size(); ++i) sprite_rom16_[i] = uint16_t(roms.sprites[i * 2] << 8 | roms.sprites[i * 2 + 1]);
    }
    sprite_rom_.resize(roms.sprites.size() / 4);
    for (size_t i = 0; i < sprite_rom_.size(); ++i)
        sprite_rom_[i] = uint32_t(roms.sprites[i * 4]) | uint32_t(roms.sprites[i * 4 + 1]) << 8 | uint32_t(roms.sprites[i * 4 + 2]) << 16 |
                         uint32_t(roms.sprites[i * 4 + 3]) << 24;

    // segaic16_road_outrun_decode
    roms.road.resize(0x10000, 0);
    const int len = int(roms.road.size());
    road_gfx_.resize((256 * 2 + 1) * 512);
    for (int y = 0; y < 256 * 2; ++y) {
        const uint8_t *src = roms.road.data() + ((y & 0xff) * 0x40 + (y >> 8) * 0x8000) % len;
        uint8_t *dst = road_gfx_.data() + y * 512;
        for (int x = 0; x < 512; ++x) {
            dst[x] = uint8_t(((src[x >> 3] >> (~x & 7)) & 1) << 0 | ((src[(x >> 3) + 0x4000] >> (~x & 7)) & 1) << 1);
            if (x >= 256 - 8 && x < 256 && dst[x] == 3) dst[x] |= 4;
        }
    }
    std::memset(road_gfx_.data() + 256 * 2 * 512, 3, 512);

    sprite_bitmap_.assign(size_t(kWidth) * kHeight, 0xffff);

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
    const uint16_t n = cfg_.palette_entries;
    if (cfg_.hangon) {
        // hangon_paletteram_w: normal, shadow and hilight banks
        // (sharrier: paletteram_w, normal and shadow/hilight)
        const int banks = cfg_.sharrier ? 2 : 3;
        if (index >= banks * n) return 0;
        // device_palette_interface::allocate_palette: a standard rainbow
        // until the pen is first written
        if (v_.pal_written && !v_.pal_written[index % n])
            return (index & 1 ? 0xff0000u : 0) | (index & 2 ? 0x00ff00u : 0) | (index & 4 ? 0x0000ffu : 0);
        const uint16_t v = be16(v_.palette, index % n);
        const int r = ((v >> 12) & 1) | ((v << 1) & 0x1e), g = ((v >> 13) & 1) | ((v >> 3) & 0x1e), bl = ((v >> 14) & 1) | ((v >> 7) & 0x1e);
        const uint8_t *t = index < n ? pal_normal_ : index < 2 * n ? pal_shadow_ : pal_hilight_;
        if (cfg_.sharrier && index >= n) t = (v & 0x8000) ? pal_hilight_ : pal_shadow_;
        return uint32_t(t[r]) << 16 | uint32_t(t[g]) << 8 | t[bl];
    }
    if (index >= 2 * n) return 0; // the black pen
    const uint16_t v = be16(v_.palette, index & (n - 1));
    const int r = ((v >> 12) & 1) | ((v << 1) & 0x1e), g = ((v >> 13) & 1) | ((v >> 3) & 0x1e), bl = ((v >> 14) & 1) | ((v >> 7) & 0x1e);
    if (index < n) return uint32_t(pal_normal_[r]) << 16 | uint32_t(pal_normal_[g]) << 8 | pal_normal_[bl];
    const uint8_t *t = (v & 0x8000) ? pal_hilight_ : pal_shadow_;
    return uint32_t(t[r]) << 16 | uint32_t(t[g]) << 8 | t[bl];
}

// ---- tilemaps ----

// One 512x256 page tilemap (64x32 tiles, TILEMAP_SCAN_ROWS) drawn into
// [x0, x1] x [y0, y1] at scroll (xscroll, yscroll), MAME's tilemap rules:
// a pixel is drawn when its tile's category matches and its pen is not 0;
// it gets pen + color * 8 and ORs primark into the priority bitmap.
void Video::draw_page_rect(int x0, int x1, int y0, int y1, uint16_t pages, uint16_t xscroll, uint16_t yscroll, int category,
                           uint8_t primark) {
    for (int y = y0; y <= y1; ++y) {
        const int vy = (y + yscroll) & 0x1ff;
        uint16_t *dest = &bitmap[size_t(y) * kWidth];
        uint8_t *pri = &priority_[size_t(y) * kWidth];
        for (int x = x0; x <= x1; ++x) {
            const int vx = (x + xscroll) & 0x3ff;
            const int page = (pages >> ((vx >= 512 ? 4 : 0) + (vy >= 256 ? 8 : 0))) & 0xf;
            const int px = vx & 0x1ff, py = vy & 0xff;
            const uint16_t data = be16(v_.tileram, size_t(page) * 64 * 32 + (py >> 3) * 64 + (px >> 3));
            if (int(data >> 15) != category) continue;
            // tilemap_16b_tile_info (or _alt): two banks of 0x1000 tiles, bank[i] = i
            const uint32_t code = (data & 0x1fffu) % tiles_;
            const uint8_t pen = tile_pens_[size_t(code) * 64 + (py & 7) * 8 + (px & 7)];
            if (!pen) continue;
            const int color = cfg_.alt_tilemap ? (data >> 5) & 0x7f : (data >> 6) & 0x7f;
            dest[x] = uint16_t(pen + color * 8 + cfg_.tile_colorbase);
            pri[x] |= primark;
        }
    }
}

// tilemap_16b_draw_layer (no screen flip)
void Video::draw_tilemap_layer(int min_y, int max_y, int which, int category, uint8_t primark) {
    const uint16_t xscroll = v_.latched_xscroll[which], yscroll = v_.latched_yscroll[which], pages = v_.latched_pageselect[which];
    for (int y = min_y & ~7; y <= max_y; y += 8) {
        const int rowscrollindex = y / 8;
        const int y0 = std::max(y, min_y), y1 = std::min(y + 7, max_y);
        const uint16_t rowscroll = be16(v_.textram, 0xf80 / 2 + 0x40 / 2 * which + rowscrollindex);
        if (yscroll & 0x8000) {
            // column and row scroll
            for (int x = ((0 + 8) & ~15) - 8; x <= kWidth - 1; x += 16) {
                const int x0 = std::max(x, 0), x1 = std::min(x + 15, kWidth - 1);
                uint16_t effx = (xscroll & 0x8000) ? rowscroll : xscroll;
                uint16_t effy = be16(v_.textram, 0xf16 / 2 + 0x40 / 2 * which + (x + 8) / 16);
                uint16_t effpages = pages;
                if (rowscroll & 0x8000) {
                    effx = v_.latched_xscroll[which + 2];
                    effy = v_.latched_yscroll[which + 2];
                    effpages = v_.latched_pageselect[which + 2];
                }
                effx = uint16_t((0xc0 - effx + 0) & 0x3ff);
                effy &= 0x1ff;
                draw_page_rect(x0, x1, y0, y1, effpages, effx, effy, category, primark);
            }
        } else {
            uint16_t effx = (xscroll & 0x8000) ? rowscroll : xscroll;
            uint16_t effy = yscroll;
            uint16_t effpages = pages;
            if (rowscroll & 0x8000) {
                effx = v_.latched_xscroll[which + 2];
                effy = v_.latched_yscroll[which + 2];
                effpages = v_.latched_pageselect[which + 2];
            }
            effx = uint16_t((0xc0 - effx + 0) & 0x3ff);
            effy &= 0x1ff;
            draw_page_rect(0, kWidth - 1, y0, y1, effpages, effx, effy, category, primark);
        }
    }
}

// the text layer: 64x28 tiles, scrolldx -192, transparent pen 0
void Video::draw_text(int min_y, int max_y, int category, uint8_t primark) {
    for (int y = min_y; y <= max_y; ++y) {
        uint16_t *dest = &bitmap[size_t(y) * kWidth];
        uint8_t *pri = &priority_[size_t(y) * kWidth];
        for (int x = 0; x < kWidth; ++x) {
            const int px = (x + 192) & 0x1ff;
            const uint16_t data = be16(v_.textram, size_t(y >> 3) * 64 + (px >> 3));
            if (int(data >> 15) != category) continue;
            // tilemap_16b_text_info (or _alt), bank[0] = 0
            const uint32_t code = (cfg_.alt_tilemap ? data & 0xffu : data & 0x1ffu) % tiles_;
            const uint8_t pen = tile_pens_[size_t(code) * 64 + (y & 7) * 8 + (px & 7)];
            if (!pen) continue;
            const int color = cfg_.alt_tilemap ? (data >> 8) & 7 : (data >> 9) & 7;
            dest[x] = uint16_t(pen + color * 8 + cfg_.tile_colorbase);
            pri[x] |= primark;
        }
    }
}

// ---- road (segaic16_road_outrun_draw) ----

void Video::draw_road(int min_y, int max_y, bool foreground) {
    static const uint8_t priority_map[2][8] = {{0x80, 0x81, 0x81, 0x87, 0, 0, 0, 0x00}, {0x81, 0x81, 0x81, 0x8f, 0, 0, 0, 0x80}};
    const int colorbase1 = cfg_.road_colorbase[0], colorbase2 = cfg_.road_colorbase[1], colorbase3 = cfg_.road_colorbase[2], xoffs = cfg_.road_xoffs;
    auto road = [&](int w) { return be16(v_.road_buffer, size_t(w)); };
    const int ctl = *v_.road_control;
    for (int y = min_y; y <= max_y; ++y) {
        uint16_t *dest = &bitmap[size_t(y) * kWidth];
        const uint16_t data0 = road(0x000 + y), data1 = road(0x100 + y);
        if (!foreground) {
            int color = -1;
            switch (ctl & 3) {
            case 0:
                if (data0 & 0x800) color = data0 & 0x7f;
                break;
            case 1:
                if (data0 & 0x800) color = data0 & 0x7f;
                else if (data1 & 0x800) color = data1 & 0x7f;
                break;
            case 2:
                if (data1 & 0x800) color = data1 & 0x7f;
                else if (data0 & 0x800) color = data0 & 0x7f;
                break;
            case 3:
                if (data1 & 0x800) color = data1 & 0x7f;
                break;
            }
            if (color != -1) {
                color |= colorbase3;
                for (int x = 0; x < kWidth; ++x) dest[x] = uint16_t(color);
            }
            continue;
        }
        const int control = ctl & 3;
        uint16_t color_table[32] = {};
        if ((data0 & 0x800) && (data1 & 0x800)) continue;
        const uint8_t *src0 = (data0 & 0x800) ? road_gfx_.data() + 256 * 2 * 512 : road_gfx_.data() + (0x000 + ((data0 >> 1) & 0xff)) * 512;
        int hpos0 = road(0x200 + ((ctl & 4) ? y : (data0 & 0x1ff))) & 0xfff;
        const int color0 = road(0x600 + ((ctl & 4) ? y : (data0 & 0x1ff)));
        const uint8_t *src1 = (data1 & 0x800) ? road_gfx_.data() + 256 * 2 * 512 : road_gfx_.data() + (0x100 + ((data1 >> 1) & 0xff)) * 512;
        int hpos1 = road(0x400 + ((ctl & 4) ? (0x100 + y) : (data1 & 0x1ff))) & 0xfff;
        const int color1 = road(0x600 + ((ctl & 4) ? (0x100 + y) : (data1 & 0x1ff)));

        color_table[0x00] = uint16_t(colorbase1 ^ 0x00 ^ ((color0 >> 0) & 1));
        color_table[0x01] = uint16_t(colorbase1 ^ 0x02 ^ ((color0 >> 1) & 1));
        color_table[0x02] = uint16_t(colorbase1 ^ 0x04 ^ ((color0 >> 2) & 1));
        int bgcolor = (color0 >> 8) & 0xf;
        color_table[0x03] = (data0 & 0x200) ? color_table[0x00] : uint16_t(colorbase2 ^ 0x00 ^ bgcolor);
        color_table[0x07] = uint16_t(colorbase1 ^ 0x06 ^ ((color0 >> 3) & 1));
        color_table[0x10] = uint16_t(colorbase1 ^ 0x08 ^ ((color1 >> 4) & 1));
        color_table[0x11] = uint16_t(colorbase1 ^ 0x0a ^ ((color1 >> 5) & 1));
        color_table[0x12] = uint16_t(colorbase1 ^ 0x0c ^ ((color1 >> 6) & 1));
        bgcolor = (color1 >> 8) & 0xf;
        color_table[0x13] = (data1 & 0x200) ? color_table[0x10] : uint16_t(colorbase2 ^ 0x10 ^ bgcolor);
        color_table[0x17] = uint16_t(colorbase1 ^ 0x0e ^ ((color1 >> 7) & 1));

        switch (control) {
        case 0:
            if (data0 & 0x800) continue;
            hpos0 = (hpos0 - (0x5f8 + xoffs)) & 0xfff;
            for (int x = 0; x < kWidth; ++x) {
                const int pix0 = hpos0 < 0x200 ? src0[hpos0] : 3;
                dest[x] = color_table[0x00 + pix0];
                hpos0 = (hpos0 + 1) & 0xfff;
            }
            break;
        case 1:
        case 2:
            hpos0 = (hpos0 - (0x5f8 + xoffs)) & 0xfff;
            hpos1 = (hpos1 - (0x5f8 + xoffs)) & 0xfff;
            for (int x = 0; x < kWidth; ++x) {
                const int pix0 = hpos0 < 0x200 ? src0[hpos0] : 3;
                const int pix1 = hpos1 < 0x200 ? src1[hpos1] : 3;
                if ((priority_map[control - 1][pix0] >> pix1) & 1) dest[x] = color_table[0x10 + pix1];
                else dest[x] = color_table[0x00 + pix0];
                hpos0 = (hpos0 + 1) & 0xfff;
                hpos1 = (hpos1 + 1) & 0xfff;
            }
            break;
        case 3:
            if (data1 & 0x800) continue;
            hpos1 = (hpos1 - (0x5f8 + xoffs)) & 0xfff;
            for (int x = 0; x < kWidth; ++x) {
                const int pix1 = hpos1 < 0x200 ? src1[hpos1] : 3;
                dest[x] = color_table[0x10 + pix1];
                hpos1 = (hpos1 + 1) & 0xfff;
            }
            break;
        }
    }
}

// ---- sprites (sega_outrun_sprite_device::draw) ----

void Video::draw_sprites(int min_y, int max_y) {
    const int kSpriteXOrigin = cfg_.sprite_origin;
    for (int y = min_y; y <= max_y; ++y) std::fill_n(&sprite_bitmap_[size_t(y) * kWidth], kWidth, uint16_t(0xffff));
    // the clip in sprite space
    const int cmin_x = kSpriteXOrigin, cmax_x = kSpriteXOrigin + kWidth - 1;
    const int numbanks = int(sprite_rom_.size() * 4 / 0x40000);
    for (size_t s = 0; s < v_.sprite_bytes / 16; ++s) {
        const size_t w0 = s * 8;
        auto data = [&](int i) { return be16(v_.sprite_buffer, w0 + size_t(i)); };
        if (data(0) & 0x8000) break;
        const int hide = data(0) & 0x5000;
        int bank = (data(0) >> 9) & 7;
        const int top = (data(0) & 0x1ff) - 0x100;
        uint16_t addr = data(1);
        const int pitch = int16_t(uint16_t((data(2) >> 1) | ((data(4) & 0x1000) << 3))) >> 8;
        int xpos = data(2) & 0x1ff;
        int vzoom = data(3) & 0x7ff;
        const int ydelta = (data(4) & 0x8000) ? 1 : -1;
        const int flip = (~data(4) >> 14) & 1;
        const int xdelta = (data(4) & 0x2000) ? 1 : -1;
        int hzoom = data(4) & 0x7ff;
        const int height = (cfg_.xboard ? (data(5) & 0xfff) : (data(5) >> 8)) + 1;
        const int colpri = ((cfg_.xboard ? (data(6) & 0xff) : (data(5) & 0x7f)) << 4) | (((data(3) >> 12) & 7) << 12);
        if (xpos < 0x80 && xdelta < 0) xpos += 0x200;
        uint16_t scratch = addr;
        set_be16(v_.sprite_buffer, w0 + 7, scratch);
        if (hide) continue;
        bank %= numbanks;
        const uint32_t *spritedata = sprite_rom_.data() + 0x10000 * bank;
        if (vzoom < 0x40) vzoom = 0x40;
        if (hzoom < 0x40) hzoom = 0x40;
        int yacc = 0;
        const int ytarget = top + ydelta * height;
        for (int y = top; y != ytarget; y += ydelta) {
            if (y >= min_y && y <= max_y) {
                uint16_t *dest = &sprite_bitmap_[size_t(y) * kWidth] - kSpriteXOrigin;
                int xacc = 0;
                scratch = addr;
                for (int x = xpos; (xdelta > 0 && x <= cmax_x) || (xdelta < 0 && x >= cmin_x);) {
                    uint32_t pixels = spritedata[scratch];
                    if (flip) --scratch;
                    else {
                        ++scratch;
                        pixels = ((pixels << 28) & 0xf0000000) | ((pixels << 20) & 0x0f000000) | ((pixels << 12) & 0x00f00000) |
                                 ((pixels << 4) & 0x000f0000) | ((pixels >> 4) & 0x0000f000) | ((pixels >> 12) & 0x00000f00) |
                                 ((pixels >> 20) & 0x000000f0) | ((pixels >> 28) & 0x0000000f);
                    }
                    const bool last_data = (pixels & 0x0f000000) == 0x0f000000;
                    for (int k = 0; k < 8; ++k) {
                        const int pix = pixels & 0xf;
                        while (xacc < 0x200) {
                            if (x >= cmin_x && x <= cmax_x && pix != 0 && pix != 15) dest[x] = uint16_t(colpri | pix);
                            x += xdelta;
                            xacc += hzoom;
                        }
                        xacc -= 0x200;
                        pixels >>= 4;
                    }
                    if (last_data) break;
                }
                set_be16(v_.sprite_buffer, w0 + 7, scratch);
            }
            yacc += vzoom;
            addr = uint16_t(addr + pitch * (yacc >> 9));
            yacc &= 0x1ff;
        }
    }
}

// ---- System 16B sprites (sega_sys16b_sprite_device::draw, origin 184, no flip) ----

void Video::draw_sys16b_sprites(int min_y, int max_y) {
    for (int y = min_y; y <= max_y; ++y) std::fill_n(&sprite_bitmap_[size_t(y) * kWidth], kWidth, uint16_t(0xffff));
    const int origin = cfg_.sprite_origin;
    const int cmin_x = origin, cmax_x = origin + kWidth - 1;
    const int numbanks = int(sprite_rom16_.size() / 0x10000);
    uint8_t *ram = v_.sprite_buffer; // the sprite RAM itself
    for (size_t s = 0; s < v_.sprite_bytes / 16; ++s) {
        const size_t w0 = s * 8;
        auto data = [&](int i) { return be16(ram, w0 + size_t(i)); };
        if (data(2) & 0x8000) break;
        const int bottom = data(0) >> 8, top = data(0) & 0xff;
        const int xpos = data(1) & 0x1ff;
        const int hide = data(2) & 0x4000, flip = data(2) & 0x100;
        const int pitch = int8_t(data(2) & 0xff);
        uint16_t addr = data(3);
        int bank = (data(4) >> 8) & 0xf; // m_bank[] is the identity
        const int colpri = ((data(4) & 0xff) << 4) | (((data(1) >> 9) & 0xf) << 12);
        const int vzoom = (data(5) >> 5) & 0x1f, hzoom = data(5) & 0x1f;
        set_be16(ram, w0 + 7, addr);
        if (hide || top >= bottom) continue;
        if (numbanks) bank %= numbanks;
        const uint16_t *spritedata = sprite_rom16_.data() + 0x10000 * size_t(bank);
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
            uint16_t *dest = &sprite_bitmap_[size_t(y) * kWidth] - origin;
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

// ---- screen_update_outrun / screen_update_shangon / segaxbd_state::screen_update ----

void Video::update(int min_y, int max_y) {
    if (cfg_.hangon) {
        update_hangon(min_y, max_y);
        return;
    }
    // screen_update_shangon draws whatever the display enable says
    if (!cfg_.sys16b_mixer && !*v_.display_enable) {
        for (int y = min_y; y <= max_y; ++y) std::fill_n(&bitmap[size_t(y) * kWidth], kWidth, black_pen());
        return;
    }
    if (cfg_.sys16b_sprites) draw_sys16b_sprites(min_y, max_y);
    else draw_sprites(min_y, max_y);
    for (int y = min_y; y <= max_y; ++y) std::fill_n(&priority_[size_t(y) * kWidth], kWidth, uint8_t(0));
    draw_road(min_y, max_y, false);
    if (!cfg_.road_priority) draw_road(min_y, max_y, true);
    draw_tilemap_layer(min_y, max_y, 1, 0, 0x01); // background
    draw_tilemap_layer(min_y, max_y, 1, 1, 0x02);
    draw_tilemap_layer(min_y, max_y, 0, 0, 0x02); // foreground
    draw_tilemap_layer(min_y, max_y, 0, 1, 0x04);
    if (cfg_.road_priority) draw_road(min_y, max_y, true);
    // Super Hang-On raises the text layer's priority so sprites stay under the high scores
    draw_text(min_y, max_y, 0, cfg_.sys16b_mixer ? 0x08 : 0x04);
    draw_text(min_y, max_y, 1, 0x08);
    for (int y = min_y; y <= max_y; ++y) {
        uint16_t *dest = &bitmap[size_t(y) * kWidth];
        const uint16_t *src = &sprite_bitmap_[size_t(y) * kWidth];
        const uint8_t *pri = &priority_[size_t(y) * kWidth];
        for (int x = 0; x < kWidth; ++x) {
            const uint16_t pix = src[x];
            if (pix == 0xffff) continue;
            const int priority = cfg_.sys16b_mixer ? (pix >> 10) & 3 : (pix >> 12) & 3;
            if ((1 << priority) > pri[x]) {
                const bool shadow = cfg_.sys16b_mixer ? (pix & 0x03f0) == 0x03f0 : (pix & 0x400f) == 0x400a;
                if (shadow) dest[x] = uint16_t(dest[x] + cfg_.palette_entries);
                else dest[x] = uint16_t(cfg_.sprite_pen_or | (pix & cfg_.sprite_pen_mask));
            }
        }
    }
}

// ---- Hang-On (segahang.cpp) ----

// tilemap_16a_draw_layer (TILEMAP_HANGON: 4 pages, no screen flip)
void Video::draw_16a_layer(int min_y, int max_y, int which, int category, uint8_t primark) {
    auto text = [&](int word) { return be16(v_.textram, size_t(word)); };
    const uint16_t xscroll = text(0xff8 / 2 + which) & 0x1ff;
    const uint16_t yscroll = text(0xf24 / 2 + which) & 0x0ff;
    uint16_t pages = text(0xe9e / 2 - which);
    pages = uint16_t(((pages >> 4) & 0x0707) | ((pages << 4) & 0x7070));
    pages &= 0x3333;
    auto draw = [&](int x0, int x1, int y0, int y1, uint16_t effx, uint16_t effy) {
        effx = uint16_t((0xc8 - effx + 0) & 0x3ff);
        effy &= 0x1ff;
        for (int y = y0; y <= y1; ++y) {
            const int vy = (y + effy) & 0x1ff;
            uint16_t *dest = &bitmap[size_t(y) * kWidth];
            uint8_t *pri = &priority_[size_t(y) * kWidth];
            for (int x = x0; x <= x1; ++x) {
                const int vx = (x + effx) & 0x3ff;
                const int page = (pages >> ((vx >= 512 ? 4 : 0) + (vy >= 256 ? 8 : 0))) & 0xf;
                const int px = vx & 0x1ff, py = vy & 0xff;
                const uint16_t data = be16(v_.tileram, size_t(page) * 64 * 32 + (py >> 3) * 64 + (px >> 3));
                // tilemap_16a_tile_info
                if (int((data >> 12) & 1) != category) continue;
                const uint32_t code = uint32_t(((data >> 1) & 0x1000) | (data & 0xfff)) % tiles_;
                const uint8_t pen = tile_pens_[size_t(code) * 64 + (py & 7) * 8 + (px & 7)];
                if (!pen) continue;
                dest[x] = uint16_t(pen + ((data >> 5) & 0x7f) * 8 + cfg_.tile_colorbase);
                pri[x] |= primark;
            }
        }
    };
    const bool col = *v_.colscroll, row = *v_.rowscroll;
    if (col && row) {
        for (int y = min_y & ~7; y <= max_y; y += 8) {
            const int y0 = std::max(y, min_y), y1 = std::min(y + 7, max_y);
            for (int x = 0; x <= kWidth - 1; x += 16) {
                const uint16_t effx = text(0xf80 / 2 + (y / 8) * 2 + which) & 0x1ff;
                const uint16_t effy = text(0xf30 / 2 + (x / 16) * 2 + which) & 0x0ff;
                draw(x, std::min(x + 15, kWidth - 1), y0, y1, effx, effy);
            }
        }
    } else if (col) {
        for (int x = 0; x <= kWidth - 1; x += 16) {
            const uint16_t effy = text(0xf30 / 2 + (x / 16) * 2 + which) & 0x0ff;
            draw(x, std::min(x + 15, kWidth - 1), min_y, max_y, xscroll, effy);
        }
    } else if (row) {
        for (int y = min_y & ~7; y <= max_y; y += 8) {
            const int y0 = std::max(y, min_y), y1 = std::min(y + 7, max_y);
            const uint16_t effx = text(0xf80 / 2 + (y / 8) * 2 + which) & 0x1ff;
            draw(0, kWidth - 1, y0, y1, effx, yscroll);
        }
    } else
        draw(0, kWidth - 1, min_y, max_y, xscroll, yscroll);
}

// the text layer with tilemap_16a_text_info
void Video::draw_16a_text(int min_y, int max_y, int category, uint8_t primark) {
    for (int y = min_y; y <= max_y; ++y) {
        uint16_t *dest = &bitmap[size_t(y) * kWidth];
        uint8_t *pri = &priority_[size_t(y) * kWidth];
        for (int x = 0; x < kWidth; ++x) {
            const int px = (x + 192) & 0x1ff;
            const uint16_t data = be16(v_.textram, size_t(y >> 3) * 64 + (px >> 3));
            if (int((data >> 11) & 1) != category) continue;
            const uint32_t code = uint32_t(data & 0xff) % tiles_;
            const uint8_t pen = tile_pens_[size_t(code) * 64 + (y & 7) * 8 + (px & 7)];
            if (!pen) continue;
            dest[x] = uint16_t(pen + ((data >> 8) & 7) * 8 + cfg_.tile_colorbase);
            pri[x] |= primark;
        }
    }
}

// segaic16_road_hangon_draw (ROAD_HANGON), from road RAM
void Video::draw_hangon_road(int min_y, int max_y, bool foreground) {
    auto road = [&](int w) { return be16(v_.road_buffer, size_t(w)); };
    const int colorbase1 = cfg_.road_colorbase[0], colorbase2 = cfg_.road_colorbase[1];
    for (int y = min_y; y <= max_y; ++y) {
        uint16_t *dest = &bitmap[size_t(y) * kWidth];
        const uint16_t control = road(0x000 + y);
        const int hpos = road(0x100 + (control & 0xff));
        const uint16_t color0 = road(0x200 + (control & 0xff));
        const uint16_t color1 = road(0x300 + (control & 0xff));
        const int plycont = (control >> 10) & 3;
        if ((plycont == 0) == foreground) continue;
        const uint8_t *src = hangon_road_gfx_.data() + (control & 0xff) * 512;
        int ctr9m = hpos & 7;
        int ctr9n9p = (hpos >> 3) & 0xff;
        int ff9j1 = (hpos >> 11) & 1;
        int ff9j2 = 1;
        int ss8j = 0;
        for (int x = -24; x <= kWidth - 1; ++x) {
            const bool ctr9n9p_ena = ctr9m == 7;
            if ((ctr9n9p & 0xff) == 0xff) ff9j1 = 0;
            if (!(control & 0x100)) ff9j1 = 1;
            if (!cfg_.sharrier && !(control & 0x200)) ff9j2 = 1;
            int md = 3;
            // ROAD_SHARRIER: control bit 9 drives the road ROM's /CE
            if ((!cfg_.sharrier || !(control & 0x200)) && (ctr9n9p & 0xc0) == 0xc0) {
                if (ss8j & 1) md = src[((ctr9n9p & 0x3f) << 3) | ctr9m];
                else md = src[((ctr9n9p & 0x3f) << 3) | (ctr9m ^ 7)];
            }
            const int select = (ss8j >> 3) & 1;
            int color;
            if (ff9j2 && md == 3) {
                color = (color0 >> (select ? 0 : 8)) & 0x3f;
                color |= colorbase2;
            } else {
                if ((color1 & 0x80) && md == 3) md = 0;
                color = (color1 >> ((md << 1) | select)) & 1;
                color |= select << 3;
                color |= md << 1;
                color |= colorbase1;
            }
            if (x >= 0) dest[x] = uint16_t(color);
            ctr9m = (ctr9m + 1) & 7;
            if (ctr9n9p_ena) {
                if (ff9j1) ++ctr9n9p;
                else --ctr9n9p;
            }
            ff9j2 = !(!ff9j1 && (ss8j & 0x80));
            ss8j = (ss8j << 1) | ff9j1;
        }
    }
}

// sega_hangon_sprite_device::draw (origin 189, -1: sprite row y is screen
// row y + 1), straight from sprite RAM (word 7 is its scratch)
void Video::draw_hangon_sprites(int min_y, int max_y) {
    for (int y = min_y; y <= max_y; ++y) std::fill_n(&sprite_bitmap_[size_t(y) * kWidth], kWidth, uint16_t(0xffff));
    const int ox = cfg_.sprite_origin, oy = -1;
    const int cmin_x = ox, cmax_x = ox + kWidth - 1, cmin_y = min_y + oy, cmax_y = max_y + oy;
    uint8_t *ram = v_.sprite_buffer;
    const int numbanks = hangon_sprite_banks_;
    for (size_t s = 0; s < v_.sprite_bytes / 16; ++s) {
        const size_t w0 = s * 8;
        auto data = [&](int i) { return be16(ram, w0 + size_t(i)); };
        const int bottom = data(0) >> 8;
        if (bottom > 0xf0) break;
        const int top = data(0) & 0xff;
        int bank = (data(1) >> 12) & 0xf;
        const int xpos = data(1) & 0x1ff;
        const int pitch = int16_t(data(2));
        uint16_t addr = data(3);
        const int colpri = (((data(4) >> 8) & 0x3f) << 4) | (((data(4) >> 0) & 0x3) << 10);
        const int vzoom = (data(4) >> 2) & 0x3f;
        const int hzoom = vzoom << 1;
        set_be16(ram, w0 + 7, addr);
        if (top >= bottom) continue;
        if (numbanks) bank %= numbanks;
        const uint16_t *spritedata = sprite_rom16_.data() + 0x8000 * size_t(bank);
        int zaddr = (vzoom & 0x38) << 5;
        const int zmask = 1 << (vzoom & 7);
        for (int y = top; y < bottom; ++y) {
            addr = uint16_t(addr + pitch);
            if (zoom_[size_t(zaddr++) & 0x1fff] & zmask) addr = uint16_t(addr + pitch);
            if (y < cmin_y || y > cmax_y) continue;
            uint16_t *dest = &sprite_bitmap_[size_t(y - oy) * kWidth] - ox;
            int xacc = 0;
            int x = xpos;
            auto plot = [&](int pix) {
                xacc = (xacc & 0xff) + hzoom;
                if (xacc < 0x100) {
                    if (x >= cmin_x && x <= cmax_x && pix != 0 && pix != 15) dest[x] = uint16_t(colpri | pix);
                    ++x;
                }
            };
            if (!(addr & 0x8000)) {
                uint16_t scratch = uint16_t(addr - 1);
                for (; x <= cmax_x;) {
                    const uint16_t pixels = spritedata[++scratch & 0x7fff];
                    int pix = 0;
                    for (int sh = 12; sh >= 0; sh -= 4) plot(pix = (pixels >> sh) & 0xf);
                    if (pix == 15) break;
                }
                set_be16(ram, w0 + 7, scratch);
            } else {
                uint16_t scratch = uint16_t(addr + 1);
                for (; x <= cmax_x;) {
                    const uint16_t pixels = spritedata[--scratch & 0x7fff];
                    int pix = 0;
                    for (int sh = 0; sh <= 12; sh += 4) plot(pix = (pixels >> sh) & 0xf);
                    if (pix == 15) break;
                }
                set_be16(ram, w0 + 7, scratch);
            }
        }
    }
}

// sega_sharrier_sprite_device::draw (origin 189, -1), straight from sprite
// RAM (word 7 is its scratch)
void Video::draw_sharrier_sprites(int min_y, int max_y) {
    for (int y = min_y; y <= max_y; ++y) std::fill_n(&sprite_bitmap_[size_t(y) * kWidth], kWidth, uint16_t(0xffff));
    const int ox = cfg_.sprite_origin, oy = -1;
    const int cmin_x = ox, cmax_x = ox + kWidth - 1, cmin_y = min_y + oy, cmax_y = max_y + oy;
    uint8_t *ram = v_.sprite_buffer;
    const int numbanks = hangon_sprite_banks_;
    for (size_t s = 0; s < v_.sprite_bytes / 16; ++s) {
        const size_t w0 = s * 8;
        auto data = [&](int i) { return be16(ram, w0 + size_t(i)); };
        const int bottom = data(0) >> 8;
        if (bottom > 0xf0) break;
        const int top = data(0) & 0xff;
        int bank = (data(1) >> 12) & 0x7;
        const int xpos = data(1) & 0x1ff;
        const int colpri = ((data(2) >> 8) & 0xff) << 4;
        const int pitch = int(int8_t(uint8_t(data(2) << 1))) >> 1; // util::sext(data[2], 7)
        uint16_t addr = data(3);
        const int hzoom = ((data(4) >> 8) & 0x3f) << 1;
        const int vzoom = data(4) & 0x3f;
        set_be16(ram, w0 + 7, addr);
        if (top >= bottom) continue;
        if (numbanks) bank %= numbanks;
        const uint32_t *spritedata = sprite_rom_.data() + 0x8000 * size_t(bank);
        int zaddr = (vzoom & 0x38) << 5;
        const int zmask = 1 << (vzoom & 7);
        for (int y = top; y < bottom; ++y) {
            addr = uint16_t(addr + pitch);
            if (zoom_[size_t(zaddr++) & 0x1fff] & zmask) addr = uint16_t(addr + pitch);
            if (y < cmin_y || y > cmax_y) continue;
            uint16_t *dest = &sprite_bitmap_[size_t(y - oy) * kWidth] - ox;
            int xacc = 0;
            int x = xpos;
            auto plot = [&](int pix) {
                xacc = (xacc & 0xff) + hzoom;
                if (xacc < 0x100) {
                    if (x >= cmin_x && x <= cmax_x && pix != 0 && pix != 15) dest[x] = uint16_t(colpri | pix);
                    ++x;
                }
            };
            if (!(addr & 0x8000)) {
                uint16_t scratch = uint16_t(addr - 1);
                for (; x <= cmax_x;) {
                    const uint32_t pixels = spritedata[++scratch & 0x7fff];
                    int pix = 0;
                    for (int sh = 28; sh >= 0; sh -= 4) plot(pix = int((pixels >> sh) & 0xf));
                    if (pix == 15) break;
                }
                set_be16(ram, w0 + 7, scratch);
            } else {
                uint16_t scratch = uint16_t(addr + 1);
                for (; x <= cmax_x;) {
                    const uint32_t pixels = spritedata[--scratch & 0x7fff];
                    int pix = 0;
                    for (int sh = 0; sh <= 28; sh += 4) plot(pix = int((pixels >> sh) & 0xf));
                    if (pix == 15) break;
                }
                set_be16(ram, w0 + 7, scratch);
            }
        }
    }
}

// segahang_state::screen_update (Hang On mixing)
void Video::update_hangon(int min_y, int max_y) {
    if (!*v_.display_enable) {
        for (int y = min_y; y <= max_y; ++y) std::fill_n(&bitmap[size_t(y) * kWidth], kWidth, black_pen());
        return;
    }
    if (cfg_.sharrier) draw_sharrier_sprites(min_y, max_y);
    else draw_hangon_sprites(min_y, max_y);
    for (int y = min_y; y <= max_y; ++y) std::fill_n(&priority_[size_t(y) * kWidth], kWidth, uint8_t(0));
    draw_hangon_road(min_y, max_y, false);
    draw_16a_layer(min_y, max_y, 1, 0, 0x01); // background
    draw_16a_layer(min_y, max_y, 1, 1, 0x02);
    draw_16a_layer(min_y, max_y, 0, 0, 0x02); // foreground
    draw_16a_layer(min_y, max_y, 0, 1, 0x04);
    draw_hangon_road(min_y, max_y, true);
    draw_16a_text(min_y, max_y, 0, 0x08);
    draw_16a_text(min_y, max_y, 1, 0x08);
    const uint16_t n = cfg_.palette_entries;
    for (int y = min_y; y <= max_y; ++y) {
        uint16_t *dest = &bitmap[size_t(y) * kWidth];
        const uint16_t *src = &sprite_bitmap_[size_t(y) * kWidth];
        const uint8_t *pri = &priority_[size_t(y) * kWidth];
        for (int x = 0; x < kWidth; ++x) {
            const uint16_t pix = src[x];
            if (pix == 0xffff) continue;
            if (cfg_.sharrier) {
                // Space Harrier mixing
                const int priority = ((pix >> 9) & 2) | 1;
                if ((1 << priority) > pri[x]) {
                    if ((pix & 0x80f) == 0x00a) dest[x] = uint16_t(dest[x] + n);
                    else dest[x] = uint16_t(0x400 | (pix & 0x3ff));
                }
                continue;
            }
            const int priority = pix >> 10;
            if ((1 << priority) > pri[x]) {
                if ((pix & 0x3f0) == 0x3f0) dest[x] = uint16_t(dest[x] + (*v_.shadow ? n * 2 : n));
                else dest[x] = uint16_t(0x400 | (pix & 0x3ff));
            }
        }
    }
}

} // namespace orun
