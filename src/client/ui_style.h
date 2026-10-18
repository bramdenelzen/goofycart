#pragma once
// The look shared by every menu and the HUD, after the arcade racers of the
// 90s: italic type with hard shadows, black slanted panels with a red
// stripe, chrome-yellow headings and red selection bars.
#include "render/renderer.h"

#include <algorithm>
#include <cstdint>

namespace gk::ui {
using render::rgba;

inline constexpr uint32_t kGold = rgba(255, 210, 0), kGoldDeep = rgba(255, 120, 0), kInk = rgba(6, 6, 10);
inline constexpr uint32_t kRed = rgba(225, 20, 30), kRedDeep = rgba(120, 0, 10);
inline constexpr uint32_t kText = rgba(255, 255, 255), kSubtle = rgba(200, 205, 215), kWarn = rgba(255, 110, 90);
inline constexpr uint32_t kSky = rgba(80, 190, 255);
inline constexpr float kSlant = 0.12f;  // panels
inline constexpr float kBarSlant = 0.4f; // bars and buttons

inline float text_w(const char* t, float s) { return render::Renderer::ui_text_width(t, s); }
inline float W(render::Renderer& r) { return float(r.ui_extent().width); }
inline float H(render::Renderer& r) { return float(r.ui_extent().height); }

// A slanted black panel: hard drop shadow, red stripe along the top, a thin
// yellow rule underneath.
inline void panel(render::Renderer& r, float x, float y, float w, float h, float = 0.0f) {
    r.ui_slant_rect(x + 5.0f, y + 5.0f, w, h, kSlant, rgba(0, 0, 0, 120), rgba(0, 0, 0, 120));
    r.ui_slant_rect(x, y, w, h, kSlant, rgba(30, 30, 38, 235), rgba(8, 8, 12, 240));
    // Stripes along the top and bottom edges, which the lean shifts right and left.
    r.ui_slant_rect(x + h * kSlant * 0.5f, y, w, 3.0f, kSlant, kRed, kRed);
    r.ui_slant_rect(x - h * kSlant * 0.5f + 0.2f, y + h - 1.5f, w, 1.5f, kSlant, kGold, kGold);
}

// A bar: red with a yellow nose when chosen, smoked glass otherwise.
inline void pill(render::Renderer& r, float x, float y, float w, float h, bool selected) {
    if (selected) {
        r.ui_slant_rect(x + 3.0f, y + 3.0f, w, h, kBarSlant, rgba(0, 0, 0, 140), rgba(0, 0, 0, 140));
        r.ui_slant_rect(x, y, w, h, kBarSlant, rgba(255, 50, 50), kRedDeep);
        r.ui_slant_rect(x + 1.0f, y + 1.0f, w - 2.0f, h * 0.35f, kBarSlant, rgba(255, 255, 255, 70), rgba(255, 255, 255, 0));
        r.ui_slant_rect(x - 2.0f, y, 7.0f, h, kBarSlant, kGold, kGoldDeep);
    } else {
        r.ui_slant_rect(x, y, w, h, kBarSlant, rgba(255, 255, 255, 22), rgba(255, 255, 255, 10));
    }
}

inline void text_c(render::Renderer& r, float cx, float y, const char* t, uint32_t c, float s) {
    r.ui_text(cx - text_w(t, s) * 0.5f, y, t, c, s);
}
// Text on a chosen (red) bar: white, hard shadow.
inline void ink_c(render::Renderer& r, float cx, float y, const char* t, float s) { text_c(r, cx, y, t, kText, s); }

// Chrome-gold heading: yellow to orange, black outline, a deep hard shadow.
inline void heading(render::Renderer& r, float x, float y, const char* t, float s, uint32_t top = rgba(255, 240, 110),
                    uint32_t bottom = kGoldDeep) {
    render::Renderer::TextStyle st;
    st.color = top;
    st.color_bottom = bottom;
    st.scale = s;
    st.outline = kInk;
    st.outline_px = std::min(0.9f + s * 0.35f, 3.2f);
    st.shadow = rgba(0, 0, 0, 200);
    st.shadow_offset = vec2(std::max(2.0f, s * 0.9f));
    st.slant = render::Renderer::kItalic * 1.3f;
    st.tracking = render::Renderer::kTracking;
    r.ui_text_styled(x, y, t, st);
}
inline void title(render::Renderer& r, float cx, float y, const char* t, float s = 3.0f, uint32_t color = 0) {
    const float x = cx - text_w(t, s) * 0.5f;
    if (color) heading(r, x, y, t, s, color, color);
    else heading(r, x, y, t, s);
}

// The control hints: a black strip across the bottom with a red rule.
inline void footer(render::Renderer& r, const char* hint) {
    const float s = 1.15f, h = 17.0f, y = H(r) - h - 4.0f;
    r.ui_rect(0.0f, y, W(r), h, rgba(0, 0, 0, 170));
    r.ui_rect(0.0f, y, W(r), 1.5f, kRed);
    text_c(r, W(r) * 0.5f, y + (h - 7.0f * s) * 0.5f, hint, rgba(235, 235, 240), s);
}

// A column of bars centred on cx. `dim` greys out entries by bit.
inline float button_list(render::Renderer& r, float cx, float y, float w, const char* const* items, int n, int row, uint32_t dim = 0,
                         float h = 20.0f, float gap = 6.0f, float s = 1.55f) {
    for (int i = 0; i < n; ++i) {
        const bool sel = i == row, off = dim >> i & 1;
        // Each row steps left a little, down the lean of the panel.
        const float dx = -float(i) * (h + gap) * kSlant;
        pill(r, cx - w * 0.5f + dx, y, w, h, sel);
        const float ty = y + (h - 7.0f * s) * 0.5f;
        text_c(r, cx + dx, ty, items[i], sel ? kText : off ? rgba(120, 120, 130) : rgba(215, 218, 228), s);
        if (sel) r.ui_text(cx + dx + w * 0.5f - 22.0f, ty, ">>", kGold, s);
        y += h + gap;
    }
    return y;
}

// A label on the left and a "< value >" picker on the right, as one row.
inline void option_row(render::Renderer& r, float x, float y, float w, const char* label, const char* value, bool sel, float h = 16.0f) {
    if (sel) pill(r, x, y, w, h, true);
    const float ty = y + (h - 7.0f * 1.1f) * 0.5f;
    r.ui_text(x + 12.0f, ty, label, sel ? kText : kSubtle, 1.1f);
    if (value) r.ui_text(x + w - 10.0f - text_w(value, 1.1f), ty, value, sel ? kGold : kText, 1.1f);
}

// A small slanted badge with text, for places and tags.
inline void badge(render::Renderer& r, float x, float y, float w, float h, uint32_t fill, const char* t, uint32_t tc, float s) {
    r.ui_slant_rect(x, y, w, h, kBarSlant, fill, fill);
    r.ui_text(x + (w - text_w(t, s)) * 0.5f, y + (h - 7.0f * s) * 0.5f, t, tc, s);
}

} // namespace gk::ui
