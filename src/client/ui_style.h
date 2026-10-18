#pragma once
// The look shared by every menu and the HUD: rounded navy panels with a
// soft shadow, gold pill buttons and outlined text.
#include "render/renderer.h"

#include <algorithm>
#include <cstdint>

namespace gk::ui {
using render::rgba;

inline constexpr uint32_t kGold = rgba(255, 214, 48), kGoldDeep = rgba(255, 150, 30), kInk = rgba(16, 20, 48);
inline constexpr uint32_t kPanelTop = rgba(40, 56, 128, 228), kPanelBottom = rgba(16, 22, 62, 238);
inline constexpr uint32_t kText = rgba(255, 255, 255), kSubtle = rgba(190, 206, 245), kWarn = rgba(255, 130, 110);
inline constexpr uint32_t kSky = rgba(110, 200, 255);

inline float text_w(const char* t, float s) { return render::Renderer::ui_text_width(t, s); }
inline float W(render::Renderer& r) { return float(r.ui_extent().width); }
inline float H(render::Renderer& r) { return float(r.ui_extent().height); }

// A raised panel: soft shadow, light rim, navy body with a gloss on top.
inline void panel(render::Renderer& r, float x, float y, float w, float h, float radius = 10.0f) {
    r.ui_round_rect(x + 2.0f, y + 5.0f, w, h, radius, rgba(0, 0, 20, 120), rgba(0, 0, 20, 120), 9.0f);
    r.ui_round_rect(x - 1.5f, y - 1.5f, w + 3.0f, h + 3.0f, radius + 1.5f, rgba(255, 255, 255, 140), rgba(255, 255, 255, 50));
    r.ui_round_rect(x, y, w, h, radius, kPanelTop, kPanelBottom);
    r.ui_round_rect(x + 3.0f, y + 3.0f, w - 6.0f, std::min(h * 0.35f, 20.0f), std::max(radius - 3.0f, 2.0f), rgba(255, 255, 255, 30),
                    rgba(255, 255, 255, 0));
}

// A button: gold and glowing when chosen, a faint glass bar otherwise.
inline void pill(render::Renderer& r, float x, float y, float w, float h, bool selected) {
    if (selected) {
        r.ui_round_rect(x - 2.0f, y - 2.0f, w + 4.0f, h + 4.0f, h * 0.5f + 2.0f, rgba(255, 200, 60, 120), rgba(255, 140, 20, 120), 8.0f);
        r.ui_round_rect(x, y, w, h, h * 0.5f, rgba(255, 228, 90), kGoldDeep);
        r.ui_round_rect(x + 5.0f, y + 1.5f, w - 10.0f, h * 0.42f, h * 0.25f, rgba(255, 255, 255, 110), rgba(255, 255, 255, 10));
    } else {
        r.ui_round_rect(x, y, w, h, h * 0.5f, rgba(255, 255, 255, 34), rgba(255, 255, 255, 14));
    }
}

inline void text_c(render::Renderer& r, float cx, float y, const char* t, uint32_t c, float s) {
    r.ui_text(cx - text_w(t, s) * 0.5f, y, t, c, s);
}
// Dark text on a gold button: no outline.
inline void ink_c(render::Renderer& r, float cx, float y, const char* t, float s) {
    r.ui_text_ex(cx - text_w(t, s) * 0.5f, y, t, kInk, s, 0, 0.0f, false, 0.9f);
}

// Big gold heading with a thick dark outline.
inline void title(render::Renderer& r, float cx, float y, const char* t, float s = 3.0f, uint32_t color = kGold) {
    r.ui_text_ex(cx - text_w(t, s) * 0.5f, y, t, color, s, kInk, std::min(1.2f + s * 0.45f, 4.0f), true, 1.0f);
}

// The control hints along the bottom edge.
inline void footer(render::Renderer& r, const char* hint) {
    const float s = 1.25f, w = text_w(hint, s) + 24.0f, h = 17.0f;
    const float x = (W(r) - w) * 0.5f, y = H(r) - h - 7.0f;
    r.ui_round_rect(x, y, w, h, h * 0.5f, rgba(10, 14, 40, 190), rgba(10, 14, 40, 210));
    r.ui_text_ex(x + 12.0f, y + (h - 7.0f * s) * 0.5f, hint, rgba(225, 232, 255), s, 0, 0.0f, false, 0.5f);
}

// A column of buttons centred on cx. `dim` greys out entries by bit.
inline float button_list(render::Renderer& r, float cx, float y, float w, const char* const* items, int n, int row, uint32_t dim = 0,
                         float h = 21.0f, float gap = 6.0f, float s = 1.6f) {
    for (int i = 0; i < n; ++i) {
        const bool sel = i == row, off = dim >> i & 1;
        pill(r, cx - w * 0.5f, y, w, h, sel);
        const float ty = y + (h - 7.0f * s) * 0.5f;
        if (sel) ink_c(r, cx, ty, items[i], s);
        else text_c(r, cx, ty, items[i], off ? rgba(150, 156, 180) : kText, s);
        y += h + gap;
    }
    return y;
}

// A label on the left and a "< value >" picker on the right, as one row.
inline void option_row(render::Renderer& r, float x, float y, float w, const char* label, const char* value, bool sel, float h = 16.0f) {
    if (sel) pill(r, x, y, w, h, true);
    const float ty = y + (h - 7.0f * 1.15f) * 0.5f;
    if (sel) {
        r.ui_text_ex(x + 10.0f, ty, label, kInk, 1.15f, 0, 0.0f, false, 0.9f);
        if (value) r.ui_text_ex(x + w - 10.0f - text_w(value, 1.15f), ty, value, kInk, 1.15f, 0, 0.0f, false, 0.9f);
    } else {
        r.ui_text(x + 10.0f, ty, label, kSubtle, 1.15f);
        if (value) r.ui_text(x + w - 10.0f - text_w(value, 1.15f), ty, value, kText, 1.15f);
    }
}

} // namespace gk::ui
