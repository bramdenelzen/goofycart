#include "font.h"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include <stb_truetype.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>

namespace gk::render {

bool SdfFont::load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    ttf_.assign(std::istreambuf_iterator<char>(f), {});
    stbtt_fontinfo info;
    if (ttf_.empty() || !stbtt_InitFont(&info, ttf_.data(), stbtt_GetFontOffsetForIndex(ttf_.data(), 0))) return false;

    constexpr float kPx = 56.0f;     // em size the field is baked at
    constexpr int kPad = 8;          // pixels of field around each glyph
    constexpr float kOnEdge = 128.0f;
    const float scale = stbtt_ScaleForPixelHeight(&info, kPx);
    int asc = 0, desc = 0, gap = 0;
    stbtt_GetFontVMetrics(&info, &asc, &desc, &gap);
    const float em = float(asc - desc); // ScaleForPixelHeight maps ascent - descent to kPx
    ascent_ = float(asc) / em;
    int cx0, cy0, cx1, cy1;
    if (stbtt_GetCodepointBox(&info, 'H', &cx0, &cy0, &cx1, &cy1)) cap_ = float(cy1) / em;
    spread_em_ = float(kPad) / kPx;

    size_ = 512;
    std::vector<uint8_t> field(size_t(size_) * size_t(size_), 0);
    int x = 1, y = 1, row_h = 0;
    for (int c = kFirst; c <= kLast; ++c) {
        Glyph& g = glyphs_[size_t(c - kFirst)];
        int adv = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(&info, c, &adv, &lsb);
        g.advance = float(adv) / em;
        int w = 0, h = 0, xo = 0, yo = 0;
        unsigned char* sdf = stbtt_GetCodepointSDF(&info, scale, c, kPad, uint8_t(kOnEdge), kOnEdge / float(kPad), &w, &h, &xo, &yo);
        if (!sdf) continue;
        if (x + w + 1 > size_) {
            x = 1;
            y += row_h + 1;
            row_h = 0;
        }
        if (y + h + 1 > size_) {
            stbtt_FreeSDF(sdf, nullptr);
            break;
        }
        for (int j = 0; j < h; ++j) std::copy_n(sdf + j * w, w, field.begin() + (size_t(y + j) * size_t(size_) + size_t(x)));
        stbtt_FreeSDF(sdf, nullptr);
        g.x0 = float(xo) / kPx;
        g.y0 = float(yo) / kPx;
        g.x1 = float(xo + w) / kPx;
        g.y1 = float(yo + h) / kPx;
        g.u0 = float(x) / float(size_);
        g.v0 = float(y) / float(size_);
        g.u1 = float(x + w) / float(size_);
        g.v1 = float(y + h) / float(size_);
        g.valid = true;
        x += w + 1;
        row_h = std::max(row_h, h);
    }
    atlas_.assign(field.size() * 4, 255);
    for (size_t i = 0; i < field.size(); ++i) atlas_[i * 4 + 3] = field[i];

    constexpr int n = kLast - kFirst + 1;
    kern_.assign(size_t(n * n), 0.0f);
    for (int a = 0; a < n; ++a)
        for (int b = 0; b < n; ++b)
            kern_[size_t(a * n + b)] = float(stbtt_GetCodepointKernAdvance(&info, a + kFirst, b + kFirst)) / em;
    ttf_.clear();
    ttf_.shrink_to_fit();
    return true;
}

const SdfFont::Glyph& SdfFont::glyph(char c) const {
    const int i = int(static_cast<unsigned char>(c));
    if (i < kFirst || i > kLast) return glyphs_['?' - kFirst];
    return glyphs_[size_t(i - kFirst)];
}

float SdfFont::kern(char a, char b) const {
    const int ia = int(static_cast<unsigned char>(a)) - kFirst, ib = int(static_cast<unsigned char>(b)) - kFirst;
    constexpr int n = kLast - kFirst + 1;
    if (ia < 0 || ib < 0 || ia >= n || ib >= n || kern_.empty()) return 0.0f;
    return kern_[size_t(ia * n + ib)];
}

} // namespace gk::render
