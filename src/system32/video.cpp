// license:BSD-3-Clause
// copyright-holders:Aaron Giles
//
// MAME's src/mame/sega/segas32_v.cpp, transplanted: the same layer
// renderers, sprite renderer and mixer, with MAME's tilemap objects
// replaced by pages rendered from video RAM when they change (MAME's
// tilemaps are always up to date when drawn: a write marks the tiles of
// every cached page it touches). See THIRD_PARTY.md.
#include "system32/video.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace s32 {

namespace {

constexpr int MIXER_LAYER_TEXT = 0;
constexpr int MIXER_LAYER_NBG0 = 1;
constexpr int MIXER_LAYER_NBG1 = 2;
constexpr int MIXER_LAYER_NBG2 = 3;
constexpr int MIXER_LAYER_NBG3 = 4;
constexpr int MIXER_LAYER_BITMAP = 5;
constexpr int MIXER_LAYER_SPRITES = 6;
constexpr int MIXER_LAYER_BACKGROUND = 7;
constexpr int MIXER_LAYER_SPRITES_2 = 8;
constexpr int MIXER_LAYER_MULTISPR = 9;
constexpr int MIXER_LAYER_MULTISPR_2 = 10;

inline bool BIT(uint32_t v, int b) { return (v >> b) & 1; }

// util::sext
inline int16_t sext(uint16_t value, int width) { return int16_t(int16_t(value << (16 - width)) >> (16 - width)); }

struct rectangle {
    int min_x = 0, max_x = 0, min_y = 0, max_y = 0;
    rectangle &operator&=(const rectangle &src) {
        if (src.min_x > min_x) min_x = src.min_x;
        if (src.max_x < max_x) max_x = src.max_x;
        if (src.min_y > min_y) min_y = src.min_y;
        if (src.max_y < max_y) max_y = src.max_y;
        return *this;
    }
};

struct Bitmap16 {
    std::vector<uint16_t> data;
    int width = 0;
    void allocate(int w, int h) {
        width = w;
        data.assign(size_t(w) * size_t(h), 0);
    }
    uint16_t &pix(int y, int x = 0) { return data[size_t(y) * size_t(width) + size_t(x)]; }
    const uint16_t &pix(int y, int x = 0) const { return data[size_t(y) * size_t(width) + size_t(x)]; }
    void fill(uint16_t v) { std::fill(data.begin(), data.end(), v); }
    void fill(uint16_t v, const rectangle &r) {
        for (int y = r.min_y; y <= r.max_y; ++y) std::fill_n(&pix(y, r.min_x), r.max_x - r.min_x + 1, v);
    }
};

struct layer_info {
    Bitmap16 bitmap;
    std::unique_ptr<uint8_t[]> transparent;
    int num = 0;
};

struct extents_list {
    uint8_t scan_extent[256]{};
    uint16_t extent[256][16]{};
};

// one 32x16 page of 16x16 tiles (MAME's tilemap pixmap): color << 4 | pen
struct Page {
    int page = -1, bank = -1;
    uint16_t words[0x200]{};
    Bitmap16 pixmap;
};

} // namespace

struct Video::Impl {
    Video &v;
    const bool m_is_multi32;
    uint16_t *m_videoram;
    uint16_t *m_spriteram;
    std::vector<uint8_t> tiles_;        // gfx 0 (bgcharlayout), decoded to one byte per pixel
    std::vector<uint32_t> sprite_rom_;  // m_sprite_region (native dwords)
    uint32_t sprite_rom_len_ = 0;       // in dwords
    std::vector<uint32_t> m_spriteram_32bit;
    layer_info m_layer_data[11];
    std::unique_ptr<uint16_t[]> m_solid_0000, m_solid_ffff;
    std::unique_ptr<int32_t[]> m_prev_bgstartx, m_prev_bgendx, m_bgcolor_line;
    std::vector<Page> pages_;
    rectangle visarea_{0, 52 * 8 - 1, 0, 28 * 8 - 1};
    // each screen's visible area as the screen had it before the update in
    // progress (set_visarea: 416 at power on), and the cliprect drawn last
    rectangle screen_visarea_[2] = {{0, 52 * 8 - 1, 0, 28 * 8 - 1}, {0, 52 * 8 - 1, 0, 28 * 8 - 1}};
    rectangle drawn_{0, 52 * 8 - 1, 0, 28 * 8 - 1};

    Impl(Video &video, const std::vector<uint8_t> &tiles, const std::vector<uint8_t> &sprites, bool multi32)
        : v(video), m_is_multi32(multi32), m_videoram(video.videoram.data()), m_spriteram(video.spriteram.data()) {
        // bgcharlayout: 16x16, 4bpp packed, 128 bytes a tile; pixel x of a row
        // is the nibble at bit offset {0,4,16,20,8,12,24,28,32,36,48,52,40,44,56,60}
        static const int xoffs[16] = {0, 4, 16, 20, 8, 12, 24, 28, 32, 36, 48, 52, 40, 44, 56, 60};
        const size_t ntiles = tiles.size() / 128;
        tiles_.resize(ntiles * 256);
        for (size_t t = 0; t < ntiles; ++t)
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x) {
                    const int bit = xoffs[x] + 64 * y;
                    const uint8_t b = tiles[t * 128 + size_t(bit / 8)];
                    tiles_[t * 256 + size_t(y * 16 + x)] = (bit & 4) ? (b & 0x0f) : (b >> 4);
                }
        // ROM_REGION32_BE: the loaded bytes are big-endian dwords
        sprite_rom_len_ = uint32_t(sprites.size() / 4);
        sprite_rom_.resize(sprite_rom_len_);
        for (size_t i = 0; i < sprite_rom_.size(); ++i)
            sprite_rom_[i] = uint32_t(sprites[4 * i]) << 24 | uint32_t(sprites[4 * i + 1]) << 16 |
                             uint32_t(sprites[4 * i + 2]) << 8 | sprites[4 * i + 3];
        m_spriteram_32bit.resize(0x20000 / 4);
        for (int bmap = 0; bmap < 9 + (m_is_multi32 ? 2 : 0); bmap++) {
            m_layer_data[bmap].bitmap.allocate(416, 224);
            m_layer_data[bmap].transparent = std::make_unique<uint8_t[]>(256);
            std::memset(m_layer_data[bmap].transparent.get(), 0, 256);
            m_layer_data[bmap].num = bmap;
        }
        m_solid_0000 = std::make_unique<uint16_t[]>(512);
        std::fill_n(m_solid_0000.get(), 512, uint16_t(0));
        m_solid_ffff = std::make_unique<uint16_t[]>(512);
        std::fill_n(m_solid_ffff.get(), 512, uint16_t(0xffff));
        m_prev_bgstartx = std::make_unique<int32_t[]>(512);
        m_prev_bgendx = std::make_unique<int32_t[]>(512);
        m_bgcolor_line = std::make_unique<int32_t[]>(512);
        std::fill_n(&m_prev_bgstartx[0], 512, -1);
        std::fill_n(&m_prev_bgendx[0], 512, -1);
        std::fill_n(&m_bgcolor_line[0], 512, -1);
        pages_.resize(32);
        for (Page &p : pages_) p.pixmap.allocate(512, 256);
    }

    // ---- tilemap pages ----

    const Bitmap16 &page_pixmap(int page, int bank) {
        const uint16_t *src = &m_videoram[(page & 0x7f) << 9];
        Page *free = nullptr;
        for (Page &p : pages_)
            if (p.page == page && p.bank == bank) {
                if (std::memcmp(p.words, src, sizeof p.words) != 0) render_page(p, src);
                return p.pixmap;
            } else if (!free && p.page < 0)
                free = &p;
        if (!free) {
            // all in use: drop the cache (rendered again on demand)
            for (Page &p : pages_) p.page = -1;
            free = &pages_[0];
        }
        free->page = page;
        free->bank = bank;
        render_page(*free, src);
        return free->pixmap;
    }

    void render_page(Page &p, const uint16_t *src) {
        std::memcpy(p.words, src, sizeof p.words);
        const size_t ntiles = tiles_.size() / 256;
        for (int ty = 0; ty < 16; ++ty)
            for (int tx = 0; tx < 32; ++tx) {
                // get_tile_info: code (bank << 13) | data & 0x1fff, color (data >> 4) & 0x1ff, flip (data >> 14)
                const uint16_t data = src[ty * 32 + tx];
                const size_t code = ntiles ? (size_t(p.bank << 13) | (data & 0x1fff)) % ntiles : 0;
                const uint16_t color = uint16_t(((data >> 4) & 0x1ff) << 4);
                const bool fx = data & 0x4000, fy = data & 0x8000;
                const uint8_t *g = ntiles ? &tiles_[code * 256] : nullptr;
                for (int y = 0; y < 16; ++y) {
                    uint16_t *d = &p.pixmap.pix(ty * 16 + y, tx * 16);
                    const int sy = fy ? 15 - y : y;
                    for (int x = 0; x < 16; ++x) d[x] = uint16_t(color | (g ? g[sy * 16 + (fx ? 15 - x : x)] : 0));
                }
            }
    }

    // ---- clipping ----

    bool compute_clipping_extents(bool enable, bool clipout, int clipmask, const rectangle &cliprect, extents_list *list) {
        const bool flip = BIT(m_videoram[0x1ff00 / 2], 9);
        const bool perlineclip2 = BIT(m_videoram[0x1ff04 / 2], 4);
        const bool perlineclip3 = BIT(m_videoram[0x1ff04 / 2], 5);
        rectangle tempclip = cliprect;
        tempclip.max_x++;
        tempclip.max_y++;
        list->extent[0][0] = uint16_t(tempclip.min_x);
        list->extent[0][1] = uint16_t(tempclip.max_x);
        if (!enable) {
            std::memset(&list->scan_extent[tempclip.min_y], 0, sizeof(list->scan_extent[0]) * size_t(tempclip.max_y - tempclip.min_y));
            return true;
        }
        rectangle clips[5];
        int sorted[5];
        for (int i = 0; i < 5; i++) {
            if (!flip) {
                clips[i].min_x = m_videoram[0x1ff60 / 2 + i * 4] & 0x1ff;
                clips[i].min_y = m_videoram[0x1ff62 / 2 + i * 4] & 0x0ff;
                clips[i].max_x = (m_videoram[0x1ff64 / 2 + i * 4] & 0x1ff) + 1;
                clips[i].max_y = (m_videoram[0x1ff66 / 2 + i * 4] & 0x0ff) + 1;
            } else {
                clips[i].max_x = (visarea_.max_x + 1) - (m_videoram[0x1ff60 / 2 + i * 4] & 0x1ff);
                clips[i].max_y = (visarea_.max_y + 1) - (m_videoram[0x1ff62 / 2 + i * 4] & 0x0ff);
                clips[i].min_x = (visarea_.max_x + 1) - ((m_videoram[0x1ff64 / 2 + i * 4] & 0x1ff) + 1);
                clips[i].min_y = (visarea_.max_y + 1) - ((m_videoram[0x1ff66 / 2 + i * 4] & 0x0ff) + 1);
            }
            clips[i] &= tempclip;
            sorted[i] = i;
        }
        for (int i = 1; i < 5; i++) {
            int j = i - 1;
            const int key = sorted[i];
            while (j >= 0 && clips[sorted[j]].min_x > clips[key].min_x) {
                sorted[j + 1] = sorted[j];
                j--;
            }
            sorted[j + 1] = key;
        }
        for (int i = 1; i < 32; i++) {
            if (i & clipmask) {
                uint16_t *extent = &list->extent[i][0];
                *extent++ = uint16_t(tempclip.min_x);
                for (int j = 0; j < 5; j++) {
                    if (BIT(uint32_t(i), sorted[j])) {
                        const rectangle &cur = clips[sorted[j]];
                        if (extent != &list->extent[i][1] && cur.min_x <= extent[-1]) {
                            if (cur.max_x > extent[-1]) extent[-1] = uint16_t(cur.max_x);
                        } else {
                            *extent++ = uint16_t(cur.min_x);
                            *extent++ = uint16_t(cur.max_x);
                        }
                    }
                }
                *extent++ = uint16_t(tempclip.max_x);
            }
        }
        for (int y = tempclip.min_y; y < tempclip.max_y; y++) {
            int sect = 0;
            for (int i = 0; i < 5; i++)
                if ((BIT(uint32_t(clipmask), i)) && y >= clips[i].min_y && y < clips[i].max_y) sect |= 1 << i;
            if (perlineclip2 || perlineclip3) {
                rectangle lineclips[5];
                int linesorted[5];
                const int line = flip ? (visarea_.max_y - y) : y;
                const uint16_t *table = &m_videoram[(m_videoram[0x1ff04 / 2] >> 10) * 0x400];
                for (int i = 0; i < 5; i++) {
                    lineclips[i] = clips[i];
                    linesorted[i] = i;
                }
                if (perlineclip2 && BIT(uint32_t(clipmask), 2)) {
                    const uint16_t minx = table[0x000 + line], maxx = table[0x200 + line];
                    if (minx == 0xffff || maxx == 0xffff) sect &= ~(1 << 2);
                    else if (!flip) {
                        lineclips[2].min_x = minx & 0x1ff;
                        lineclips[2].max_x = (maxx & 0x1ff) + 1;
                    } else {
                        lineclips[2].min_x = (visarea_.max_x + 1) - ((maxx & 0x1ff) + 1);
                        lineclips[2].max_x = (visarea_.max_x + 1) - (minx & 0x1ff);
                    }
                    lineclips[2] &= tempclip;
                    if (lineclips[2].min_x >= lineclips[2].max_x) sect &= ~(1 << 2);
                }
                if (perlineclip3 && BIT(uint32_t(clipmask), 3)) {
                    const uint16_t minx = table[0x100 + line], maxx = table[0x300 + line];
                    if (minx == 0xffff || maxx == 0xffff) sect &= ~(1 << 3);
                    else if (!flip) {
                        lineclips[3].min_x = minx & 0x1ff;
                        lineclips[3].max_x = (maxx & 0x1ff) + 1;
                    } else {
                        lineclips[3].min_x = (visarea_.max_x + 1) - ((maxx & 0x1ff) + 1);
                        lineclips[3].max_x = (visarea_.max_x + 1) - (minx & 0x1ff);
                    }
                    lineclips[3] &= tempclip;
                    if (lineclips[3].min_x >= lineclips[3].max_x) sect &= ~(1 << 3);
                }
                for (int i = 1; i < 5; i++) {
                    int j = i - 1;
                    const int key = linesorted[i];
                    while (j >= 0 && lineclips[linesorted[j]].min_x > lineclips[key].min_x) {
                        linesorted[j + 1] = linesorted[j];
                        j--;
                    }
                    linesorted[j + 1] = key;
                }
                // MAME writes the per-line extents at 32 + y (its extent array
                // is [256]: lines past 223 would overrun it; ours are 224)
                uint16_t *extent = &list->extent[32 + y][0];
                *extent++ = uint16_t(tempclip.min_x);
                for (int j = 0; j < 5; j++) {
                    if (BIT(uint32_t(sect), linesorted[j])) {
                        const rectangle &cur = lineclips[linesorted[j]];
                        if (extent != &list->extent[32 + y][1] && cur.min_x <= extent[-1]) {
                            if (cur.max_x > extent[-1]) extent[-1] = uint16_t(cur.max_x);
                        } else {
                            *extent++ = uint16_t(cur.min_x);
                            *extent++ = uint16_t(cur.max_x);
                        }
                    }
                }
                *extent++ = uint16_t(tempclip.max_x);
                list->scan_extent[y] = uint8_t(32 + y);
            } else {
                list->scan_extent[y] = uint8_t(sect);
            }
        }
        return clipout;
    }

    void compute_tilemap_flips(int bgnum, bool &flipx, bool &flipy) {
        const bool global_flip = BIT(m_videoram[0x1ff00 / 2], 9);
        const bool layer_flip = BIT(m_videoram[0x1ff00 / 2], bgnum);
        const bool prohibit_flipy = BIT(m_videoram[0x1ff00 / 2], 8);
        flipx = layer_flip ? !global_flip : global_flip;
        flipy = (layer_flip && !prohibit_flipy) ? !global_flip : global_flip;
    }

    void get_tilemaps(int bgnum, const Bitmap16 **tilemaps) {
        int tilebank;
        if (m_is_multi32) tilebank = (v.tilebank_external >> (2 * bgnum)) & 3;
        else tilebank = ((v.tilebank_external & 1) << 1) | ((m_videoram[0x1ff00 / 2] & 0x400) >> 10);
        tilemaps[0] = &page_pixmap((m_videoram[0x1ff40 / 2 + 2 * bgnum + 0] >> 0) & 0x7f, tilebank);
        tilemaps[1] = &page_pixmap((m_videoram[0x1ff40 / 2 + 2 * bgnum + 0] >> 8) & 0x7f, tilebank);
        tilemaps[2] = &page_pixmap((m_videoram[0x1ff40 / 2 + 2 * bgnum + 1] >> 0) & 0x7f, tilebank);
        tilemaps[3] = &page_pixmap((m_videoram[0x1ff40 / 2 + 2 * bgnum + 1] >> 8) & 0x7f, tilebank);
    }

    // ---- NBG0/1 ----

    void update_tilemap_zoom(layer_info &layer, const rectangle &cliprect, int bgnum) {
        Bitmap16 &bitmap = layer.bitmap;
        const Bitmap16 *tilemaps[4];
        get_tilemaps(bgnum, tilemaps);
        const int opaque = 0;
        bool flipx, flipy;
        compute_tilemap_flips(bgnum, flipx, flipy);
        const bool clipenable = BIT(m_videoram[0x1ff02 / 2], (11 + bgnum));
        const bool clipout = BIT(m_videoram[0x1ff02 / 2], (6 + bgnum));
        const int clips = (m_videoram[0x1ff06 / 2] >> (4 * bgnum)) & 0x0f;
        extents_list clip_extents;
        const bool clipdraw_start = compute_clipping_extents(clipenable, clipout, clips, cliprect, &clip_extents);

        int dstxstep = m_videoram[0x1ff50 / 2 + 2 * bgnum] & 0xfff;
        int dstystep;
        if (BIT(m_videoram[0x1ff00 / 2], 14)) dstystep = m_videoram[0x1ff52 / 2 + 2 * bgnum] & 0xfff;
        else dstystep = dstxstep;
        if (dstxstep < 0x80) dstxstep = 0x80;
        if (dstystep < 0x80) dstystep = 0x80;
        uint32_t srcxstep = uint32_t((0x200 << 20) / dstxstep);
        uint32_t srcystep = uint32_t((0x200 << 20) / dstystep);
        uint32_t srcx_start = uint32_t(m_videoram[0x1ff12 / 2 + 4 * bgnum] & 0x3ff) << 20;
        srcx_start += uint32_t(m_videoram[0x1ff10 / 2 + 4 * bgnum] & 0xff00) << 4;
        uint32_t srcy = uint32_t(m_videoram[0x1ff16 / 2 + 4 * bgnum] & 0x1ff) << 20;
        srcy += uint32_t(m_videoram[0x1ff14 / 2 + 4 * bgnum] & 0xfe00) << 4;
        srcx_start -= sext(m_videoram[0x1ff30 / 2 + 2 * bgnum], (dstxstep != 0x200) ? 10 : 9) * srcxstep;
        srcy -= sext(m_videoram[0x1ff32 / 2 + 2 * bgnum], (dstystep != 0x200) ? 10 : 9) * srcystep;
        srcx_start += cliprect.min_x * srcxstep;
        srcy += cliprect.min_y * srcystep;
        if (flipy) {
            srcy += (visarea_.max_y - 2 * cliprect.min_y) * srcystep;
            srcystep = -srcystep;
        }
        if (flipx) {
            srcx_start += (visarea_.max_x - 2 * cliprect.min_x) * srcxstep;
            srcxstep = -srcxstep;
        }
        for (int y = cliprect.min_y; y <= cliprect.max_y; y++) {
            const uint16_t *extents = &clip_extents.extent[clip_extents.scan_extent[y]][0];
            uint16_t *const dst = &bitmap.pix(y);
            bool clipdraw = clipdraw_start;
            if (clipdraw || extents[1] <= cliprect.max_x) {
                int transparent = 0;
                const Bitmap16 &tm0 = *tilemaps[((srcy >> 27) & 2) + 0];
                const Bitmap16 &tm1 = *tilemaps[((srcy >> 27) & 2) + 1];
                const uint16_t *const src[2] = {&tm0.pix((srcy >> 20) & 0xff), &tm1.pix((srcy >> 20) & 0xff)};
                uint32_t srcx = srcx_start;
                while (true) {
                    if (clipdraw) {
                        for (int x = extents[0]; x < extents[1]; x++) {
                            uint16_t pix = src[(srcx >> 29) & 1][(srcx >> 20) & 0x1ff];
                            srcx += srcxstep;
                            if ((pix & 0x0f) == 0 && !opaque) pix = 0, transparent++;
                            dst[x] = pix;
                        }
                    } else {
                        const int pixels = extents[1] - extents[0];
                        std::fill_n(&dst[extents[0]], pixels, uint16_t(0));
                        srcx += srcxstep * uint32_t(pixels);
                        transparent += pixels;
                    }
                    if (extents[1] > cliprect.max_x) break;
                    clipdraw = !clipdraw;
                    extents++;
                }
                layer.transparent[y] = (transparent == cliprect.max_x - cliprect.min_x + 1);
            } else
                layer.transparent[y] = 1;
            srcy += srcystep;
        }
    }

    // ---- NBG2/3 ----

    void update_tilemap_rowscroll(layer_info &layer, const rectangle &cliprect, int bgnum) {
        Bitmap16 &bitmap = layer.bitmap;
        const Bitmap16 *tilemaps[4];
        get_tilemaps(bgnum, tilemaps);
        const int opaque = 0;
        bool flipx, flipy;
        compute_tilemap_flips(bgnum, flipx, flipy);
        const bool clipenable = BIT(m_videoram[0x1ff02 / 2], (11 + bgnum));
        const bool clipout = BIT(m_videoram[0x1ff02 / 2], (6 + bgnum));
        const int clips = (m_videoram[0x1ff06 / 2] >> (4 * bgnum)) & 0x0f;
        extents_list clip_extents;
        const bool clipdraw_start = compute_clipping_extents(clipenable, clipout, clips, cliprect, &clip_extents);
        bool rowscroll = BIT(m_videoram[0x1ff04 / 2], (bgnum - 2));
        bool rowselect = BIT(m_videoram[0x1ff04 / 2], bgnum);
        if (BIT(m_videoram[0x1ff04 / 2], (bgnum + 2))) rowscroll = rowselect = false;
        const uint16_t *const table = &m_videoram[(m_videoram[0x1ff04 / 2] >> 10) * 0x400];
        const int xscroll = (m_videoram[0x1ff12 / 2 + 4 * bgnum] & 0x3ff) - (m_videoram[0x1ff30 / 2 + 2 * bgnum] & 0x1ff);
        const int yscroll = (m_videoram[0x1ff16 / 2 + 4 * bgnum] & 0x1ff);
        for (int y = cliprect.min_y; y <= cliprect.max_y; y++) {
            const uint16_t *extents = &clip_extents.extent[clip_extents.scan_extent[y]][0];
            uint16_t *const dst = &bitmap.pix(y);
            bool clipdraw = clipdraw_start;
            if (clipdraw || extents[1] <= cliprect.max_x) {
                int transparent = 0;
                int srcxstep, srcx;
                if (!flipx) {
                    srcx = cliprect.min_x + xscroll;
                    srcxstep = 1;
                } else {
                    srcx = cliprect.max_x + xscroll;
                    srcxstep = -1;
                }
                int srcy, ylookup;
                if (!flipy) {
                    srcy = yscroll + y;
                    ylookup = y;
                } else {
                    srcy = yscroll + visarea_.max_y - y;
                    ylookup = visarea_.max_y - y;
                }
                if (rowscroll) srcx += table[0x000 + 0x100 * (bgnum - 2) + ylookup] & 0x3ff;
                if (rowselect) srcy = (yscroll + table[0x200 + 0x100 * (bgnum - 2) + ylookup]) & 0x1ff;
                const Bitmap16 &tm0 = *tilemaps[((srcy >> 7) & 2) + 0];
                const Bitmap16 &tm1 = *tilemaps[((srcy >> 7) & 2) + 1];
                const uint16_t *const src[2] = {&tm0.pix(srcy & 0xff), &tm1.pix(srcy & 0xff)};
                while (true) {
                    if (clipdraw) {
                        for (int x = extents[0]; x < extents[1]; x++, srcx += srcxstep) {
                            uint16_t pix = src[(srcx >> 9) & 1][srcx & 0x1ff];
                            if ((pix & 0x0f) == 0 && !opaque) pix = 0, transparent++;
                            dst[x] = pix;
                        }
                    } else {
                        const int pixels = extents[1] - extents[0];
                        std::fill_n(&dst[extents[0]], pixels, uint16_t(0));
                        srcx += srcxstep * pixels;
                        transparent += pixels;
                    }
                    if (extents[1] > cliprect.max_x) break;
                    clipdraw = !clipdraw;
                    extents++;
                }
                layer.transparent[y] = (transparent == cliprect.max_x - cliprect.min_x + 1);
            } else
                layer.transparent[y] = 1;
        }
    }

    // ---- text layer: 64x32 8x8 tiles, graphics from video RAM
    // (gfx_8x8x4_packed_msb), pen 0 transparent ----

    void update_tilemap_text(layer_info &layer, const rectangle &cliprect) {
        Bitmap16 &bitmap = layer.bitmap;
        bitmap.fill(0, cliprect);
        const bool flip = BIT(m_videoram[0x1ff00 / 2], 9);
        const uint16_t ctrl = m_videoram[0x1ff5c / 2];
        const uint16_t *map = &m_videoram[((ctrl >> 4) & 0x1f) << 11];
        const uint8_t *gfx = reinterpret_cast<const uint8_t *>(m_videoram);
        // a flipped tilemap drawn with no scroll: the screen's visible area
        // flipped (tilemap_t::draw with TILEMAP_FLIPX | TILEMAP_FLIPY)
        for (int y = cliprect.min_y; y <= cliprect.max_y; y++) {
            const int sy = (flip ? visarea_.max_y - y : y) & 0xff;
            uint16_t *dst = &bitmap.pix(y);
            for (int x = cliprect.min_x; x <= cliprect.max_x; x++) {
                const int sx = (flip ? visarea_.max_x - x : x) & 0x1ff;
                const uint16_t data = map[(sy >> 3) * 64 + (sx >> 3)];
                const uint32_t code = (uint32_t(ctrl & 7) << 9) | (data & 0x1ff);
                const uint8_t b = gfx[code * 32 + uint32_t(sy & 7) * 4 + uint32_t((sx & 7) >> 1)];
                const int pen = (sx & 1) ? (b & 0x0f) : (b >> 4);
                if (pen) dst[x] = uint16_t((((data >> 9) & 0x7f) << 4) | pen);
            }
        }
    }

    // ---- bitmap layer ----

    void update_bitmap(layer_info &layer, const rectangle &cliprect) {
        Bitmap16 &bitmap = layer.bitmap;
        const int bpp = BIT(m_videoram[0x1ff00 / 2], 11) ? 8 : 4;
        const bool clipenable = BIT(m_videoram[0x1ff02 / 2], 15);
        const bool clipout = BIT(m_videoram[0x1ff02 / 2], 10);
        const int clips = 0x10;
        extents_list clip_extents;
        const bool clipdraw_start = compute_clipping_extents(clipenable, clipout, clips, cliprect, &clip_extents);
        const int xscroll = m_videoram[0x1ff88 / 2] & 0x1ff;
        const int yscroll = m_videoram[0x1ff8a / 2] & 0x1ff;
        const int color = (m_videoram[0x1ff8c / 2] << 4) & 0x1fff0 & ~((1 << bpp) - 1);
        for (int y = cliprect.min_y; y <= cliprect.max_y; y++) {
            const uint16_t *extents = &clip_extents.extent[clip_extents.scan_extent[y]][0];
            uint16_t *const dst = &bitmap.pix(y);
            bool clipdraw = clipdraw_start;
            if (clipdraw || extents[1] <= cliprect.max_x) {
                int transparent = 0;
                while (true) {
                    if (clipdraw) {
                        if (bpp == 8) {
                            const uint8_t *src = reinterpret_cast<const uint8_t *>(&m_videoram[512 / 2 * ((y + yscroll) & 0xff)]);
                            for (int x = extents[0]; x < extents[1]; x++) {
                                const int effx = (x + xscroll) & 0x1ff;
                                int pix = src[effx] + color;
                                if ((pix & 0xff) == 0) pix = 0, transparent++;
                                dst[x] = uint16_t(pix);
                            }
                        } else {
                            const uint16_t *src = &m_videoram[512 / 4 * ((y + yscroll) & 0x1ff)];
                            for (int x = extents[0]; x < extents[1]; x++) {
                                const int effx = (x + xscroll) & 0x1ff;
                                int pix = ((src[effx / 4] >> (4 * (effx & 3))) & 0x0f) + color;
                                if ((pix & 0x0f) == 0) pix = 0, transparent++;
                                dst[x] = uint16_t(pix);
                            }
                        }
                    } else {
                        const int pixels = extents[1] - extents[0];
                        std::fill_n(&dst[extents[0]], pixels, uint16_t(0));
                        transparent += pixels;
                    }
                    if (extents[1] > cliprect.max_x) break;
                    clipdraw = !clipdraw;
                    extents++;
                }
                layer.transparent[y] = (transparent == cliprect.max_x - cliprect.min_x + 1);
            } else
                layer.transparent[y] = 1;
        }
    }

    void update_background(layer_info &layer, const rectangle &cliprect) {
        Bitmap16 &bitmap = layer.bitmap;
        const bool flip = BIT(m_videoram[0x1ff00 / 2], 9);
        for (int y = cliprect.min_y; y <= cliprect.max_y; y++) {
            uint16_t *const dst = &bitmap.pix(flip ? (cliprect.max_y - y) : y);
            int color;
            if (BIT(m_videoram[0x1ff5e / 2], 15)) {
                const int yoffset = (m_videoram[0x1ff5e / 2] + y) & 0x1ff;
                color = (m_videoram[0x1ff5e / 2] & 0x1e00) + yoffset;
            } else
                color = m_videoram[0x1ff5e / 2] & 0x1e00;
            if ((m_bgcolor_line[y & 0x1ff] != color) || (m_prev_bgstartx[y & 0x1ff] != cliprect.min_x) ||
                (m_prev_bgendx[y & 0x1ff] != cliprect.max_x)) {
                for (int x = cliprect.min_x; x <= cliprect.max_x; x++) dst[x] = uint16_t(color);
                m_prev_bgstartx[y & 0x1ff] = cliprect.min_x;
                m_prev_bgendx[y & 0x1ff] = cliprect.max_x;
                m_bgcolor_line[y & 0x1ff] = color;
            }
        }
    }

    uint8_t update_tilemaps(const rectangle &cliprect) {
        const int enable0 = !BIT(m_videoram[0x1ff02 / 2], 0) && !BIT(m_videoram[0x1ff8e / 2], 1);
        const int enable1 = !BIT(m_videoram[0x1ff02 / 2], 1) && !BIT(m_videoram[0x1ff8e / 2], 2);
        const int enable2 = !BIT(m_videoram[0x1ff02 / 2], 2) && !BIT(m_videoram[0x1ff8e / 2], 3) && !BIT(m_videoram[0x1ff00 / 2], 12);
        const int enable3 = !BIT(m_videoram[0x1ff02 / 2], 3) && !BIT(m_videoram[0x1ff8e / 2], 4) && !BIT(m_videoram[0x1ff00 / 2], 13);
        const int enablet = !BIT(m_videoram[0x1ff02 / 2], 4) && !BIT(m_videoram[0x1ff8e / 2], 0);
        const int enableb = !BIT(m_videoram[0x1ff02 / 2], 5) && !BIT(m_videoram[0x1ff8e / 2], 5);
        if (enable0) update_tilemap_zoom(m_layer_data[MIXER_LAYER_NBG0], cliprect, 0);
        if (enable1) update_tilemap_zoom(m_layer_data[MIXER_LAYER_NBG1], cliprect, 1);
        if (enable2) update_tilemap_rowscroll(m_layer_data[MIXER_LAYER_NBG2], cliprect, 2);
        if (enable3) update_tilemap_rowscroll(m_layer_data[MIXER_LAYER_NBG3], cliprect, 3);
        if (enablet) update_tilemap_text(m_layer_data[MIXER_LAYER_TEXT], cliprect);
        if (enableb) update_bitmap(m_layer_data[MIXER_LAYER_BITMAP], cliprect);
        update_background(m_layer_data[MIXER_LAYER_BACKGROUND], cliprect);
        return uint8_t((enablet << 0) | (enable0 << 1) | (enable1 << 2) | (enable2 << 3) | (enable3 << 4) | (enableb << 5));
    }

    // ---- sprites ----

    void sprite_erase_buffer() {
        m_layer_data[MIXER_LAYER_SPRITES].bitmap.fill(0xffff);
        if (m_is_multi32) m_layer_data[MIXER_LAYER_MULTISPR].bitmap.fill(0xffff);
    }

    void swap_layers(int a, int b) {
        std::swap(m_layer_data[a].bitmap, m_layer_data[b].bitmap);
        std::swap(m_layer_data[a].transparent, m_layer_data[b].transparent);
        std::swap(m_layer_data[a].num, m_layer_data[b].num);
    }

    void sprite_swap_buffers() {
        swap_layers(MIXER_LAYER_SPRITES, MIXER_LAYER_SPRITES_2);
        if (m_is_multi32) swap_layers(MIXER_LAYER_MULTISPR, MIXER_LAYER_MULTISPR_2);
        std::memcpy(v.sprite_control_latched, v.sprite_control, sizeof v.sprite_control_latched);
    }

    int draw_one_sprite(const uint16_t *data, int xoffs, int yoffs, const rectangle &clipin, const rectangle &clipout);
    void sprite_render_list();

    // ---- mixer ----

    uint8_t compute_color_offsets(int which, int layerbit, int layerflag) {
        const int mode = ((v.mixer[which][0x3e / 2] & 0x8000) >> 14) | (layerbit & 1);
        switch (mode) {
        case 0:
        case 3:
        default: return uint8_t(!layerflag);
        case 1: return 2;
        case 2: return (!layerflag) ? 2 : 0;
        }
    }

    static uint16_t compute_sprite_blend(uint8_t encoding) {
        const int value = encoding & 0xf;
        switch ((encoding >> 4) & 3) {
        case 0: return uint16_t(1 << value);
        case 1: return uint16_t((1 << value) | ((1 << value) - 1));
        case 2: return uint16_t(~((1 << value) - 1) & 0xffff);
        default: return 0xffff;
        }
    }

    uint16_t *get_layer_scanline(int layer, int scanline) {
        if (m_layer_data[layer].transparent[scanline]) return (layer == MIXER_LAYER_SPRITES) ? m_solid_ffff.get() : m_solid_0000.get();
        return &m_layer_data[layer].bitmap.pix(scanline);
    }

    void mix_all_layers(int which, uint32_t *out, int pitch, const rectangle &cliprect, uint8_t enablemask);
};

// ---- sprite rendering (draw_one_sprite, sprite_render_list) ----

#define sprite_draw_pixel_16(trans)                                                                                   \
    if (x >= clipin.min_x && x <= clipin.max_x && (!do_clipout || x < clipout.min_x || x > clipout.max_x) && pix != trans) { \
        if (!indirect) {                                                                                              \
            if (pix != 0) {                                                                                           \
                if (!shadow) dest[x] = uint16_t(color | pix);                                                         \
                else dest[x] &= 0x7fff;                                                                               \
            }                                                                                                         \
        } else {                                                                                                      \
            const int indpix = indtable[pix];                                                                         \
            if ((indpix & transmask) != transmask) {                                                                  \
                if (!shadow) dest[x] = uint16_t(indpix);                                                              \
                else dest[x] &= 0x7fff;                                                                               \
            }                                                                                                         \
        }                                                                                                             \
    }

#define sprite_draw_pixel_256(trans)                                                                                  \
    if (x >= clipin.min_x && x <= clipin.max_x && (!do_clipout || x < clipout.min_x || x > clipout.max_x) && pix != trans) { \
        if (!indirect) {                                                                                              \
            if (pix != 0) {                                                                                           \
                if (!shadow) dest[x] = uint16_t(color | pix);                                                         \
                else dest[x] &= 0x7fff;                                                                               \
            }                                                                                                         \
        } else {                                                                                                      \
            const int indpix = (indtable[pix >> 4]) | (pix & 0x0f);                                                   \
            if ((indpix & transmask) != transmask) {                                                                  \
                if (!shadow) dest[x] = uint16_t(indpix);                                                              \
                else dest[x] &= 0x7fff;                                                                               \
            }                                                                                                         \
        }                                                                                                             \
    }

int Video::Impl::draw_one_sprite(const uint16_t *data, int xoffs, int yoffs, const rectangle &clipin, const rectangle &clipout) {
    static const int transparency_masks[4][4] = {
        {0x7fff, 0x3fff, 0x1fff, 0x0fff}, {0x3fff, 0x1fff, 0x0fff, 0x07ff}, {0x3fff, 0x1fff, 0x0fff, 0x07ff}, {0x1fff, 0x0fff, 0x07ff, 0x03ff}};
    Bitmap16 &bitmap = m_layer_data[(!m_is_multi32 || !BIT(data[3], 11)) ? MIXER_LAYER_SPRITES_2 : MIXER_LAYER_MULTISPR_2].bitmap;
    const uint8_t numbanks = uint8_t(sprite_rom_len_ >> 20); // MAME: m_sprite_region.length() is in dwords
    const uint8_t *latched = v.sprite_control_latched;

    const bool indirect = BIT(data[0], 13);
    const bool indlocal = BIT(data[0], 12);
    const bool shadow = BIT(data[0], 11) && BIT(latched[0x0a / 2], 0);
    const bool fromram = BIT(data[0], 10);
    const bool bpp8 = BIT(data[0], 9);
    const int transp = BIT(data[0], 8) ? 0 : (bpp8 ? 0xff : 0x0f);
    const bool flipy = BIT(data[0], 7);
    const bool flipx = BIT(data[0], 6);
    const bool offsety = BIT(data[0], 5);
    const bool offsetx = BIT(data[0], 4);
    const int adjusty = (data[0] >> 2) & 3;
    const int adjustx = (data[0] >> 0) & 3;
    const int srch = (data[1] >> 8);
    const int srcw = bpp8 ? (data[1] & 0x3f) : ((data[1] >> 1) & 0x3f);
    int bank = m_is_multi32 ? ((data[3] & 0x2000) >> 13) | ((data[3] & 0x8000) >> 14)
                            : ((data[3] & 0x0800) >> 11) | ((data[3] & 0x4000) >> 13);
    const int dsth = data[2] & 0x3ff;
    const int dstw = data[3] & 0x3ff;
    int ypos = sext(data[4], 12);
    int xpos = sext(data[5], 12);
    uint32_t addr = data[6] | ((data[2] & 0xf000) << 4);
    const int color = 0x8000 | (data[7] & (bpp8 ? 0x7f00 : 0x7ff0));
    int hzoom, vzoom;
    int xdelta = 1, ydelta = 1;
    int xtarget, ytarget, yacc = 0, pix, transmask;
    const uint32_t *spritedata;
    uint32_t addrmask;
    uint16_t indtable[16]{};

    if (srcw == 0 || srch == 0 || dstw == 0 || dsth == 0) goto bail;

    transmask = transparency_masks[latched[0x08 / 2] & 3][latched[0x0a / 2] & 3];
    if (bpp8) transmask &= 0xfff0;

    if (indirect) {
        const uint16_t *src = indlocal ? &data[8] : &m_spriteram[8 * (data[7] & 0x1fff)];
        for (int x = 0; x < 16; x++)
            indtable[x] = uint16_t((src[x] & (bpp8 ? 0xfff0 : 0xffff)) | (BIT(latched[0x0a / 2], 0) ? 0x8000 : 0x0000));
    }

    if (fromram) {
        spritedata = m_spriteram_32bit.data();
        addrmask = (0x20000 / 4) - 1;
    } else {
        if (numbanks) bank %= numbanks;
        spritedata = &sprite_rom_[size_t(bank) << 20]; // m_sprite_region[bank << 20] (dwords)
        addrmask = 0xfffff;
    }

    hzoom = (((bpp8 ? 4 : 8) * srcw) << 16) / dstw;
    vzoom = (srch << 16) / dsth;

    if (offsetx) xpos += xoffs;
    switch (adjustx) {
    case 0:
    case 3: xpos -= (dstw - 1) / 2; break;
    case 1: xpos -= dstw - 1; break;
    case 2: break;
    }
    if (offsety) ypos += yoffs;
    switch (adjusty) {
    case 0:
    case 3: ypos -= (dsth - 1) / 2; break;
    case 1: ypos -= dsth - 1; break;
    case 2: break;
    }
    if (flipx) {
        xpos += dstw - 1;
        xdelta = -1;
    }
    if (flipy) {
        ypos += dsth - 1;
        ydelta = -1;
    }
    xtarget = xpos + xdelta * dstw;
    ytarget = ypos + ydelta * dsth;
    if (xdelta > 0 && xtarget > clipin.max_x) {
        xtarget = clipin.max_x + 1;
        if (xpos >= xtarget) goto bail;
    }
    if (xdelta < 0 && xtarget < clipin.min_x) {
        xtarget = clipin.min_x - 1;
        if (xpos <= xtarget) goto bail;
    }

    for (int y = ypos; y != ytarget; y += ydelta) {
        if (y >= clipin.min_y && y <= clipin.max_y) {
            const int do_clipout = (y >= clipout.min_y && y <= clipout.max_y);
            uint16_t *const dest = &bitmap.pix(y);
            int xacc = 0;
            if (!bpp8) {
                uint32_t curaddr = addr - 1;
                for (int x = xpos; x != xtarget;) {
                    const uint32_t pixels = spritedata[++curaddr & addrmask];
                    pix = (pixels >> 28) & 0xf; while (xacc < 0x10000 && x != xtarget) { sprite_draw_pixel_16(transp); x += xdelta; xacc += hzoom; } xacc -= 0x10000;
                    pix = (pixels >> 24) & 0xf; while (xacc < 0x10000 && x != xtarget) { sprite_draw_pixel_16(0); x += xdelta; xacc += hzoom; } xacc -= 0x10000;
                    pix = (pixels >> 20) & 0xf; while (xacc < 0x10000 && x != xtarget) { sprite_draw_pixel_16(0); x += xdelta; xacc += hzoom; } xacc -= 0x10000;
                    pix = (pixels >> 16) & 0xf; while (xacc < 0x10000 && x != xtarget) { sprite_draw_pixel_16(0); x += xdelta; xacc += hzoom; } xacc -= 0x10000;
                    pix = (pixels >> 12) & 0xf; while (xacc < 0x10000 && x != xtarget) { sprite_draw_pixel_16(0); x += xdelta; xacc += hzoom; } xacc -= 0x10000;
                    pix = (pixels >> 8) & 0xf; while (xacc < 0x10000 && x != xtarget) { sprite_draw_pixel_16(0); x += xdelta; xacc += hzoom; } xacc -= 0x10000;
                    pix = (pixels >> 4) & 0xf; while (xacc < 0x10000 && x != xtarget) { sprite_draw_pixel_16(0); x += xdelta; xacc += hzoom; } xacc -= 0x10000;
                    pix = (pixels >> 0) & 0xf; while (xacc < 0x10000 && x != xtarget) { sprite_draw_pixel_16(transp); x += xdelta; xacc += hzoom; } xacc -= 0x10000;
                    if (transp != 0 && pix == 0x0f) break;
                }
            } else {
                uint32_t curaddr = addr - 1;
                for (int x = xpos; x != xtarget;) {
                    const uint32_t pixels = spritedata[++curaddr & addrmask];
                    pix = (pixels >> 24) & 0xff; while (xacc < 0x10000 && x != xtarget) { sprite_draw_pixel_256(transp); x += xdelta; xacc += hzoom; } xacc -= 0x10000;
                    pix = (pixels >> 16) & 0xff; while (xacc < 0x10000 && x != xtarget) { sprite_draw_pixel_256(0); x += xdelta; xacc += hzoom; } xacc -= 0x10000;
                    pix = (pixels >> 8) & 0xff; while (xacc < 0x10000 && x != xtarget) { sprite_draw_pixel_256(0); x += xdelta; xacc += hzoom; } xacc -= 0x10000;
                    pix = (pixels >> 0) & 0xff; while (xacc < 0x10000 && x != xtarget) { sprite_draw_pixel_256(transp); x += xdelta; xacc += hzoom; } xacc -= 0x10000;
                    if (transp != 0 && pix == 0xff) break;
                }
            }
        }
        yacc += vzoom;
        addr += uint32_t(srcw * (yacc >> 16));
        yacc &= 0xffff;
    }

bail:
    return (indirect && indlocal) ? 2 : 0;
}

void Video::Impl::sprite_render_list() {
    // m_spriteram_32bit, as spriteram_w keeps it
    for (int i = 0; i < 0x20000 / 4; ++i) {
        const uint16_t lo = m_spriteram[2 * i], hi = m_spriteram[2 * i + 1];
        m_spriteram_32bit[size_t(i)] = ((hi >> 8) & 0x000000ffu) | ((uint32_t(hi) << 8) & 0x0000ff00u) |
                                       ((uint32_t(lo) << 8) & 0x00ff0000u) | ((uint32_t(lo) << 24) & 0xff000000u);
    }
    rectangle outerclip, clipin, clipout;
    int xoffs = 0, yoffs = 0;
    int numentries = 0;
    int spritenum = 0;
    outerclip.min_x = outerclip.min_y = 0;
    outerclip.max_x = BIT(v.sprite_control_latched[0x0c / 2], 0) ? 415 : 319;
    outerclip.max_y = 223;
    clipin = outerclip;
    clipout.min_x = clipout.min_y = 0;
    clipout.max_x = clipout.max_y = -1;
    while (numentries++ < 0x20000 / 16) {
        const uint16_t *const sprite = &m_spriteram[8 * (spritenum & 0x1fff)];
        switch (sprite[0] >> 14) {
        case 0: spritenum += 1 + draw_one_sprite(sprite, xoffs, yoffs, clipin, clipout); break;
        case 1:
            if (BIT(sprite[0], 12)) {
                clipin.min_y = sext(sprite[0], 12);
                clipin.max_y = sext(sprite[1], 12);
                clipin.min_x = sext(sprite[2], 12);
                clipin.max_x = sext(sprite[3], 12);
                clipin &= outerclip;
            }
            if (BIT(sprite[0], 13)) {
                clipout.min_y = sext(sprite[4], 12);
                clipout.max_y = sext(sprite[5], 12);
                clipout.min_x = sext(sprite[6], 12);
                clipout.max_x = sext(sprite[7], 12);
            }
            spritenum++;
            break;
        case 2:
            if (BIT(sprite[0], 13)) {
                yoffs = sext(sprite[1], 12);
                xoffs = sext(sprite[2], 12);
            }
            spritenum = sprite[0] & 0x1fff;
            break;
        case 3: numentries = 0x20000 / 16; break;
        }
    }
}

// ---- the mixer ----

void Video::Impl::mix_all_layers(int which, uint32_t *out, int pitch, const rectangle &cliprect, uint8_t enablemask) {
    const uint16_t *mc = v.mixer[which];
    const bool blendenable = BIT(mc[0x4e / 2], 11);
    const int blendfactor = (mc[0x4e / 2] >> 8) & 7;
    struct mixer_layer_info {
        uint16_t palbase;
        uint16_t sprblendmask;
        uint8_t blendmask;
        uint8_t index;
        uint8_t effpri;
        uint8_t mixshift;
        uint8_t coloroffs;
    } layerorder[16][8], layersort[8];

    if (which == 1) swap_layers(MIXER_LAYER_SPRITES, MIXER_LAYER_MULTISPR);

    int rgboffs[3][3];
    rgboffs[0][0] = sext(mc[0x40 / 2], 6);
    rgboffs[0][1] = sext(mc[0x42 / 2], 6);
    rgboffs[0][2] = sext(mc[0x44 / 2], 6);
    rgboffs[1][0] = sext(mc[0x46 / 2], 6);
    rgboffs[1][1] = sext(mc[0x48 / 2], 6);
    rgboffs[1][2] = sext(mc[0x4a / 2], 6);
    rgboffs[2][0] = rgboffs[2][1] = rgboffs[2][2] = 0;

    uint8_t sprgroup_shift, sprgroup_mask, sprgroup_or;
    switch (mc[0x4c / 2] & 0x0f) {
    default:
    case 0x0: sprgroup_shift = 14; sprgroup_mask = 0x00; sprgroup_or = 0x01; break;
    case 0x1: sprgroup_shift = 14; sprgroup_mask = 0x01; sprgroup_or = 0x02; break;
    case 0x2: sprgroup_shift = 13; sprgroup_mask = 0x03; sprgroup_or = 0x04; break;
    case 0x3: sprgroup_shift = 12; sprgroup_mask = 0x07; sprgroup_or = 0x08; break;
    case 0x4: sprgroup_shift = 14; sprgroup_mask = 0x01; sprgroup_or = 0x00; break;
    case 0x5: sprgroup_shift = 13; sprgroup_mask = 0x03; sprgroup_or = 0x00; break;
    case 0x6: sprgroup_shift = 12; sprgroup_mask = 0x07; sprgroup_or = 0x00; break;
    case 0x7: sprgroup_shift = 11; sprgroup_mask = 0x0f; sprgroup_or = 0x00; break;
    case 0x8: sprgroup_shift = 14; sprgroup_mask = 0x01; sprgroup_or = 0x00; break;
    case 0x9: sprgroup_shift = 13; sprgroup_mask = 0x03; sprgroup_or = 0x00; break;
    case 0xa: sprgroup_shift = 12; sprgroup_mask = 0x07; sprgroup_or = 0x00; break;
    case 0xb: sprgroup_shift = 11; sprgroup_mask = 0x0f; sprgroup_or = 0x00; break;
    case 0xc: sprgroup_shift = 13; sprgroup_mask = 0x01; sprgroup_or = 0x00; break;
    case 0xd: sprgroup_shift = 12; sprgroup_mask = 0x03; sprgroup_or = 0x00; break;
    case 0xe: sprgroup_shift = 11; sprgroup_mask = 0x07; sprgroup_or = 0x00; break;
    case 0xf: sprgroup_shift = 10; sprgroup_mask = 0x0f; sprgroup_or = 0x00; break;
    }
    const int sprshadowmask = BIT(mc[0x4c / 2], 2) ? 0x8000 : 0x0000;
    const int sprpixmask = ((1 << sprgroup_shift) - 1) & 0x3fff;
    const int sprshadow = 0x7ffe & sprpixmask;

    int numlayers = 0;
    for (int laynum = MIXER_LAYER_TEXT; laynum <= MIXER_LAYER_BITMAP; laynum++) {
        const int priority = mc[0x20 / 2 + laynum] & 0x0f;
        if (BIT(enablemask, laynum) && priority != 0) {
            layersort[numlayers].index = uint8_t(laynum);
            layersort[numlayers].effpri = uint8_t((priority << 3) | (6 - laynum));
            layersort[numlayers].palbase = uint16_t((mc[0x20 / 2 + laynum] & 0x00f0) << 6);
            layersort[numlayers].mixshift = uint8_t((mc[0x20 / 2 + laynum] >> 8) & 3);
            layersort[numlayers].blendmask = uint8_t(blendenable ? ((mc[0x30 / 2 + laynum] >> 6) & 0xff) : 0);
            layersort[numlayers].sprblendmask = compute_sprite_blend(uint8_t(mc[0x30 / 2 + laynum] & 0x3f));
            layersort[numlayers].coloroffs = compute_color_offsets(which, BIT(mc[0x3e / 2], laynum), BIT(mc[0x30 / 2 + laynum], 14));
            numlayers++;
        }
    }
    layersort[numlayers].index = MIXER_LAYER_BACKGROUND;
    layersort[numlayers].effpri = (1 << 3) | 0;
    layersort[numlayers].palbase = uint16_t((mc[0x2c / 2] & 0x00f0) << 6);
    layersort[numlayers].mixshift = uint8_t((mc[0x2c / 2] >> 8) & 3);
    layersort[numlayers].blendmask = 0;
    layersort[numlayers].sprblendmask = 0;
    layersort[numlayers].coloroffs = compute_color_offsets(which, BIT(mc[0x3e / 2], 8), BIT(mc[0x3e / 2], 14));
    numlayers++;

    for (int laynum = 0; laynum < numlayers; laynum++)
        for (int i = laynum + 1; i < numlayers; i++)
            if (layersort[i].effpri > layersort[laynum].effpri) std::swap(layersort[i], layersort[laynum]);

    for (int groupnum = 0; groupnum <= sprgroup_mask; groupnum++) {
        const int effgroup = sprgroup_or | groupnum;
        const int priority = mc[0x00 / 2 + effgroup] & 0x0f;
        const int effpri = (priority << 3) | 7;
        int sprindex = numlayers;
        int dstnum = 0;
        for (int laynum = 0; laynum < numlayers; laynum++) {
            if (effpri > layersort[laynum].effpri && sprindex == numlayers) sprindex = dstnum++;
            layerorder[groupnum][dstnum++] = layersort[laynum];
        }
        layerorder[groupnum][sprindex].index = MIXER_LAYER_SPRITES;
        layerorder[groupnum][sprindex].effpri = uint8_t(effpri);
        if ((mc[0x4c / 2] & 3) != 3) layerorder[groupnum][sprindex].palbase = uint16_t((mc[0x00 / 2 + effgroup] & 0x00f0) << 6);
        else layerorder[groupnum][sprindex].palbase = uint16_t((mc[0x4c / 2] & 0x00f0) << 6);
        layerorder[groupnum][sprindex].mixshift = uint8_t((mc[0x00 / 2 + effgroup] >> 8) & 3);
        layerorder[groupnum][sprindex].blendmask = 0;
        layerorder[groupnum][sprindex].sprblendmask = 0;
        layerorder[groupnum][sprindex].coloroffs = compute_color_offsets(which, BIT(mc[0x3e / 2], 6), BIT(mc[0x4c / 2], 15));
    }

    int sprx_start, sprdx;
    if (BIT(v.sprite_control_latched[0x04 / 2], 0)) {
        sprx_start = cliprect.max_x;
        sprdx = -1;
    } else {
        sprx_start = cliprect.min_x;
        sprdx = 1;
    }
    int spry, sprdy;
    if (BIT(v.sprite_control_latched[0x04 / 2], 1)) {
        spry = cliprect.max_y;
        sprdy = -1;
    } else {
        spry = cliprect.min_y;
        sprdy = 1;
    }

    const uint16_t *pal = v.paletteram[which];
    for (int y = cliprect.min_y; y <= cliprect.max_y; y++, spry += sprdy) {
        uint32_t *const dest = out + size_t(y) * size_t(pitch);
        uint16_t *layerbase[8];
        layerbase[MIXER_LAYER_TEXT] = get_layer_scanline(MIXER_LAYER_TEXT, y);
        layerbase[MIXER_LAYER_NBG0] = get_layer_scanline(MIXER_LAYER_NBG0, y);
        layerbase[MIXER_LAYER_NBG1] = get_layer_scanline(MIXER_LAYER_NBG1, y);
        layerbase[MIXER_LAYER_NBG2] = get_layer_scanline(MIXER_LAYER_NBG2, y);
        layerbase[MIXER_LAYER_NBG3] = get_layer_scanline(MIXER_LAYER_NBG3, y);
        layerbase[MIXER_LAYER_BITMAP] = get_layer_scanline(MIXER_LAYER_BITMAP, y);
        layerbase[MIXER_LAYER_SPRITES] = get_layer_scanline(MIXER_LAYER_SPRITES, spry);
        layerbase[MIXER_LAYER_BACKGROUND] = get_layer_scanline(MIXER_LAYER_BACKGROUND, y);
        for (int x = cliprect.min_x, sprx = sprx_start; x <= cliprect.max_x; x++, sprx += sprdx) {
            const mixer_layer_info *first;
            int laynum, firstpix;
            int shadow = 0;
            const int sprpix = layerbase[MIXER_LAYER_SPRITES][sprx];
            const int sprgroup = (sprpix >> sprgroup_shift) & sprgroup_mask;
            for (first = &layerorder[sprgroup][0];; first++) {
                laynum = first->index;
                if (laynum != MIXER_LAYER_SPRITES) {
                    firstpix = layerbase[laynum][x] & 0x1fff;
                    if (firstpix != 0 || laynum == MIXER_LAYER_BACKGROUND) break;
                } else {
                    firstpix = sprpix;
                    shadow = ~firstpix & sprshadowmask;
                    if ((firstpix & 0x7fff) != 0x7fff) {
                        firstpix &= sprpixmask;
                        if ((firstpix & 0x7ffe) != sprshadow) break;
                        shadow = 1;
                    }
                }
            }
            firstpix = pal[(first->palbase + ((firstpix >> first->mixshift) & 0xfff0) + (firstpix & 0x0f)) & 0x3fff];
            const int *rgbdelta = &rgboffs[first->coloroffs][0];
            int r = ((firstpix >> 0) & 0x1f) + rgbdelta[0];
            int g = ((firstpix >> 5) & 0x1f) + rgbdelta[1];
            int b = ((firstpix >> 10) & 0x1f) + rgbdelta[2];
            if (first->blendmask != 0) {
                const mixer_layer_info *second;
                int secondpix;
                for (second = first + 1;; second++) {
                    laynum = second->index;
                    if (laynum != MIXER_LAYER_SPRITES) {
                        secondpix = layerbase[laynum][x] & 0x1fff;
                        if (secondpix != 0 || laynum == MIXER_LAYER_BACKGROUND) break;
                    } else {
                        secondpix = sprpix;
                        shadow = ~secondpix & sprshadowmask;
                        if ((secondpix & 0x7fff) != 0x7fff) {
                            secondpix &= sprpixmask;
                            if ((secondpix & 0x7ffe) != sprshadow) break;
                            shadow = 1;
                        }
                    }
                }
                if (BIT(first->blendmask, laynum) && (laynum != MIXER_LAYER_SPRITES || BIT(first->sprblendmask, sprgroup))) {
                    secondpix = pal[(second->palbase + ((secondpix >> second->mixshift) & 0xfff0) + (secondpix & 0x0f)) & 0x3fff];
                    r *= 7 - blendfactor;
                    g *= 7 - blendfactor;
                    b *= 7 - blendfactor;
                    rgbdelta = &rgboffs[second->coloroffs][0];
                    r += (((secondpix >> 0) & 0x1f) + rgbdelta[0]) * (blendfactor + 1);
                    g += (((secondpix >> 5) & 0x1f) + rgbdelta[1]) * (blendfactor + 1);
                    b += (((secondpix >> 10) & 0x1f) + rgbdelta[2]) * (blendfactor + 1);
                    r >>= 3;
                    g >>= 3;
                    b >>= 3;
                }
            }
            if (shadow) {
                r >>= 1;
                g >>= 1;
                b >>= 1;
            }
            if (r > 31) firstpix = 31 << (16 + 3);
            else if (r > 0) firstpix = r << (16 + 3);
            else firstpix = 0;
            if (g > 31) firstpix |= 31 << (8 + 3);
            else if (g > 0) firstpix |= g << (8 + 3);
            if (b > 31) firstpix |= 31 << (0 + 3);
            else if (b > 0) firstpix |= b << (0 + 3);
            dest[x] = uint32_t(firstpix);
        }
    }

    if (which == 1) swap_layers(MIXER_LAYER_SPRITES, MIXER_LAYER_MULTISPR);
}

// ---- Video ----

Video::Video(const std::vector<uint8_t> &tiles, const std::vector<uint8_t> &sprites, bool multi32)
    : p_(std::make_unique<Impl>(*this, tiles, sprites, multi32)), multi32_(multi32) {}

Video::~Video() = default;

void Video::power_on() {
    // device_start: video RAM cleared but for 0x1ff00 (416 pixels wide),
    // the mixers 0xff
    videoram.fill(0);
    videoram[0x1ff00 / 2] = 0x8000;
    std::memset(mixer, 0xff, sizeof mixer[0][0] * 0x80);
}

int Video::width() const { return p_->drawn_.max_x + 1; }

uint16_t Video::palette_r(int which, uint32_t offset) const {
    const bool convert = (offset >> 14) & 1;
    offset &= 0x3fff;
    const uint16_t value = paletteram[which][offset];
    if (!convert) return value;
    // xBBBBBGGGGGRRRRR_to_xBGRBBBBGGGGRRRR
    const int r = (value >> 0) & 0x1f, g = (value >> 5) & 0x1f, b = (value >> 10) & 0x1f;
    uint16_t v = uint16_t((value & 0x8000) | ((b & 0x01) << 14) | ((g & 0x01) << 13) | ((r & 0x01) << 12));
    v = uint16_t(v | ((b & 0x1e) << 7) | ((g & 0x1e) << 3) | ((r & 0x1e) >> 1));
    return v;
}

void Video::palette_w(int which, uint32_t offset, uint16_t data, uint16_t mem_mask) {
    const bool convert = (offset >> 14) & 1;
    offset &= 0x3fff;
    auto to_bgr = [](uint16_t value) {
        const int r = (value >> 0) & 0x1f, g = (value >> 5) & 0x1f, b = (value >> 10) & 0x1f;
        uint16_t v = uint16_t((value & 0x8000) | ((b & 0x01) << 14) | ((g & 0x01) << 13) | ((r & 0x01) << 12));
        return uint16_t(v | ((b & 0x1e) << 7) | ((g & 0x1e) << 3) | ((r & 0x1e) >> 1));
    };
    auto from_bgr = [](uint16_t value) {
        const int r = ((value >> 12) & 0x01) | ((value << 1) & 0x1e);
        const int g = ((value >> 13) & 0x01) | ((value >> 3) & 0x1e);
        const int b = ((value >> 14) & 0x01) | ((value >> 7) & 0x1e);
        return uint16_t((value & 0x8000) | (b << 10) | (g << 5) | (r << 0));
    };
    auto put = [&](uint32_t off) {
        uint16_t value = paletteram[which][off];
        if (convert) value = to_bgr(value);
        value = uint16_t((value & ~mem_mask) | (data & mem_mask));
        if (convert) value = from_bgr(value);
        paletteram[which][off] = value;
    };
    put(offset);
    // with blending enabled, writes go to both halves
    if (mixer[which][0x4e / 2] & 0x0880) put(offset ^ 0x2000);
}

uint8_t Video::sprite_control_r(int offset) const {
    switch (offset) {
    case 0: return uint8_t(0xfc | int(p_->m_layer_data[MIXER_LAYER_SPRITES].num < p_->m_layer_data[MIXER_LAYER_SPRITES_2].num));
    case 1: return 0xfc | 1;
    case 2: return uint8_t(0xfc | sprite_control_latched[2]);
    case 3: return uint8_t(0xfc | sprite_control_latched[3]);
    case 4: return uint8_t(0xfc | sprite_control_latched[4]);
    case 5: return uint8_t(0xfc | sprite_control_latched[5]);
    case 6: return uint8_t(0xfc | (sprite_control_latched[6] & 1));
    case 7: return 0xfc;
    }
    return 0xff;
}

void Video::update_sprites() {
    if (!((sprite_control[3] >> 1) & 1)) {
        if (sprite_render_count_-- == 0) {
            sprite_control[0] = 3;
            sprite_render_count_ = uint8_t(sprite_control[3] & 1);
        }
    }
    if ((sprite_control[0] >> 1) & 1) p_->sprite_erase_buffer();
    if (sprite_control[0] & 1) {
        p_->sprite_swap_buffers();
        p_->sprite_render_list();
    }
    sprite_control[0] = 0;
}

void Video::render(int index, uint32_t *out) {
    // multi32_update: the visible area from 0x1ff00 bit 15 (416 or 320)
    // the update draws the screen's visible area as it was (the cliprect
    // MAME's screen passes in); set_visible_area takes effect from the next
    // frame, though the clips below already see the new one
    const rectangle cliprect = p_->screen_visarea_[index];
    p_->visarea_ = {0, (videoram[0x1ff00 / 2] & 0x8000) ? 52 * 8 - 1 : 40 * 8 - 1, 0, 28 * 8 - 1};
    p_->screen_visarea_[index] = p_->visarea_;
    p_->drawn_ = cliprect;
    if (!displayenable[index]) {
        for (int y = 0; y < kHeight; ++y) std::fill_n(out + size_t(y) * kWidth, kWidth, 0u);
        return;
    }
    const uint8_t enablemask = p_->update_tilemaps(cliprect);
    p_->mix_all_layers(index, out, kWidth, cliprect, enablemask);
}

} // namespace s32
