#pragma once
// A painted texture: an RGBA canvas that models draw their albedo into, one
// rectangle of UV space per part, with soft shading, highlights, spots,
// stripes and faces baked in, the way hand-painted game textures look.
#include "gk/core/math.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace gk::render {

// A region of UV space, 0..1 on each axis.
struct UvRect {
    float x0 = 0.0f, y0 = 0.0f, x1 = 1.0f, y1 = 1.0f;
    float w() const { return x1 - x0; }
    float h() const { return y1 - y0; }
    vec2 at(float u, float v) const { return {x0 + u * w(), y0 + v * h()}; }
};

class Canvas {
public:
    explicit Canvas(int size = 512, vec4 clear = vec4(0.5f, 0.5f, 0.5f, 1.0f));
    int size() const { return size_; }
    const std::vector<uint8_t>& rgba() const { return rgba_; }

    // All colours are 0..1 RGBA; painting blends by the colour's alpha.
    void fill(const UvRect& r, vec4 color);
    // Top colour at v = 0 fading to bottom at v = 1 (v runs down the texture,
    // which on a lathe or sphere is from the top of the shape down).
    void gradient_v(const UvRect& r, vec4 top, vec4 bottom);
    void gradient_u(const UvRect& r, vec4 left, vec4 right);
    // A soft round highlight or shadow at (u, v) within the rect.
    void glow(const UvRect& r, vec2 uv, float radius, vec4 color, float softness = 1.0f);
    // Darkens towards the rect's edges, a cheap baked-in occlusion.
    void vignette(const UvRect& r, float strength, float width = 0.25f);
    // A horizontal band between v0 and v1 (a stripe around a lathe).
    void band_v(const UvRect& r, float v0, float v1, vec4 color, float soft = 0.0f);
    void band_u(const UvRect& r, float u0, float u1, vec4 color, float soft = 0.0f);
    // `n` round spots, radius in rect units, placed by `seed`.
    void spots(const UvRect& r, int n, float radius, vec4 color, uint32_t seed, float soft = 0.3f);
    // Stripes along u (count across the rect), alternating the two colours.
    void stripes_u(const UvRect& r, int count, vec4 a, vec4 b, float slant = 0.0f);
    // Checker cells across the rect.
    void checker(const UvRect& r, int cells, vec4 a, vec4 b);
    // A cartoon eye: white ellipse, iris, pupil and a highlight.
    void eye(const UvRect& r, vec2 uv, vec2 radius, vec4 iris, float lid = 0.0f);
    // An eyebrow or closed eye: a thick dark arc.
    void arc(const UvRect& r, vec2 center, float radius, float a0, float a1, float thickness, vec4 color);
    // A mouth: a curved line, smiling when `curve` > 0.
    void mouth(const UvRect& r, vec2 uv, float width, float curve, float thickness, vec4 color);
    // A filled ellipse.
    void ellipse(const UvRect& r, vec2 uv, vec2 radius, vec4 color, float soft = 0.02f);
    // A filled, rounded rectangle inside the rect.
    void rounded(const UvRect& r, UvRect inner, float corner, vec4 color);
    // Fine grain or cloudy noise, mixed in by `amount`.
    void noise(const UvRect& r, float amount, float scale, uint32_t seed, vec4 tint = vec4(1.0f));
    // A bolt of lightning / crack: a jagged line from a to b.
    void jagged(const UvRect& r, vec2 a, vec2 b, float thickness, vec4 color, uint32_t seed, float wobble = 0.08f);
    // Brick courses or window grids for buildings.
    void bricks(const UvRect& r, int cols, int rows, vec4 mortar, vec4 brick, uint32_t seed);
    void windows(const UvRect& r, int cols, int rows, vec4 frame, vec4 lit, vec4 dark, uint32_t seed, float lit_chance = 0.6f);
    // Text-like glyph: a bold "?" or a letter drawn from strokes.
    void question_mark(const UvRect& r, vec2 uv, float height, float thickness, vec4 color);
    void star(const UvRect& r, vec2 uv, float radius, vec4 color);

    // Paints any function of (u, v) in rect units; returns RGBA to blend.
    void custom(const UvRect& r, const std::function<vec4(float, float)>& f);

    // Per-pixel access, uv in rect units.
    vec4 get(const UvRect& r, float u, float v) const;
    void blend_px(int x, int y, vec4 color);

private:
    int size_;
    std::vector<uint8_t> rgba_;
    // Runs `f(u, v)` over the rect's pixels; f returns the colour to blend.
    template <class F> void paint(const UvRect& r, F&& f);
};

} // namespace gk::render
