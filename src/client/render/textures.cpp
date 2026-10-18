#include "textures.h"

#include <algorithm>
#include <cmath>

namespace gk::render {
namespace {

constexpr int N = kTexSize;
constexpr int S = N / 32; // the patterns were drawn at 32 px; scale their pixel sizes

uint32_t hash(int x, int y, uint32_t seed) {
    uint32_t h = uint32_t(x) * 374761393u + uint32_t(y) * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
float rnd(int x, int y, uint32_t seed) { return float(hash(x, y, seed) & 0xffff) / 65535.0f; }

// Value noise that tiles every N pixels, `cells` cells across.
float noise(float x, float y, int cells, uint32_t seed) {
    const float fx = x * float(cells) / float(N), fy = y * float(cells) / float(N);
    const int x0 = int(std::floor(fx)), y0 = int(std::floor(fy));
    const float tx = fx - float(x0), ty = fy - float(y0);
    auto at = [&](int cx, int cy) { return rnd(((cx % cells) + cells) % cells, ((cy % cells) + cells) % cells, seed); };
    auto s = [](float t) { return t * t * (3.0f - 2.0f * t); };
    const float a = at(x0, y0), b = at(x0 + 1, y0), c = at(x0, y0 + 1), d = at(x0 + 1, y0 + 1);
    return a + (b - a) * s(tx) + (c - a) * s(ty) + (a - b - c + d) * s(tx) * s(ty);
}
float fbm(float x, float y, uint32_t seed) {
    return 0.5f * noise(x, y, 4, seed) + 0.3f * noise(x, y, 8, seed + 1) + 0.2f * noise(x, y, 16, seed + 2);
}

struct Texel {
    float r, g, b;
};
using Fn = Texel (*)(int x, int y);
Texel grey(float v) { return {v, v, v}; }

// Values are brightness multipliers around 1 (the shader scales 0..255 to 0..2).
Texel white(int, int) { return grey(1.0f); }
Texel asphalt(int x, int y) {
    float v = 0.92f + 0.12f * fbm(float(x), float(y), 11);
    const float s = rnd(x, y, 12);
    if (s > 0.93f) v += 0.1f;
    else if (s < 0.06f) v -= 0.1f;
    return grey(v);
}
Texel grass(int x, int y) {
    float v = 0.85f + 0.2f * noise(float(x), float(y) * 0.35f, 8, 21) + 0.1f * fbm(float(x), float(y), 22);
    if (rnd(x, y, 23) > 0.9f) v += 0.12f;
    return {v * 0.97f, v * 1.03f, v * 0.95f};
}
Texel dirt(int x, int y) {
    float v = 0.8f + 0.3f * fbm(float(x), float(y), 31);
    if (rnd(x / (2 * S), y / (2 * S), 32) > 0.92f) v = 1.15f; // pebbles
    return grey(v);
}
Texel sand(int x, int y) {
    const float ripple = std::sin((float(y) + 2.0f * S * noise(float(x), float(y), 4, 41)) * 2.0f * 3.14159f / (8.0f * S));
    return grey(0.95f + 0.08f * ripple + 0.06f * rnd(x, y, 42));
}
Texel snow(int x, int y) { return grey(0.97f + 0.06f * fbm(float(x), float(y), 51) + 0.03f * rnd(x, y, 52)); }
Texel ice(int x, int y) {
    float v = 0.95f + 0.1f * fbm(float(x), float(y), 61);
    const float crack = std::abs(noise(float(x), float(y), 4, 62) - 0.5f);
    if (crack < 0.03f) v += 0.2f;
    return {v * 0.97f, v, v * 1.04f};
}
Texel water(int x, int y) {
    const float w = std::sin((float(x) * 0.5f + float(y) + 4.0f * S * noise(float(x), float(y), 4, 71)) * 2.0f * 3.14159f / (8.0f * S));
    return grey(0.92f + 0.1f * w + (w > 0.92f ? 0.15f : 0.0f));
}
Texel brick(int x, int y) {
    const int row = y / (8 * S), bx = (x + (row % 2) * 8 * S) % (16 * S);
    if (y % (8 * S) < S / 2 + 1 || bx < S / 2 + 1) return grey(0.65f); // mortar
    return grey(0.92f + 0.14f * rnd((x + (row % 2) * 8 * S) / (16 * S), row, 81) + 0.06f * rnd(x / S, y / S, 82) + 0.03f * fbm(float(x), float(y), 83));
}
Texel panel(int x, int y) {
    const int px = x % (16 * S), py = y % (16 * S);
    if (px < S / 2 + 1 || py < S / 2 + 1) return grey(0.7f);
    if (px < S + 1 || py < S + 1) return grey(1.12f);
    for (int rx : {3 * S, 13 * S})
        for (int ry : {3 * S, 13 * S})
            if ((px - rx) * (px - rx) + (py - ry) * (py - ry) < S * S) return grey(1.2f); // rivets
    return grey(0.95f + 0.05f * fbm(float(x), float(y), 91));
}
Texel wood(int x, int y) {
    if (x < 2 * S || x > N - 3 * S || y < 2 * S || y > N - 3 * S) return {0.8f, 0.72f, 0.62f}; // frame
    if (std::abs(x - y) < 2 * S || std::abs(x + y - N) < 2 * S) return {0.9f, 0.82f, 0.7f}; // brace
    const float grain = 0.08f * std::sin(float(y) * 1.3f / float(S) + 3.0f * noise(float(x), float(y), 4, 101));
    const float plank = (x % (10 * S) < S) ? -0.2f : 0.0f;
    const float v = 1.02f + grain + plank;
    return {v, v * 0.92f, v * 0.8f};
}
Texel rock(int x, int y) {
    float v = 0.8f + 0.35f * fbm(float(x), float(y), 111);
    const float crack = std::abs(noise(float(x), float(y), 8, 112) - 0.5f);
    if (crack < 0.035f) v -= 0.25f;
    return grey(v);
}
Texel arrows(int x, int y) {
    // Two chevrons along +v (down the image), bright on a darker ground.
    const int yy = (y + N - std::abs(x - N / 2)) % (N / 2);
    return grey(yy < 5 * S ? 1.35f : 0.8f);
}
Texel stripes(int x, int y) { return grey(((x + y) / (8 * S)) % 2 ? 1.12f : 0.82f); }
Texel leaves(int x, int y) {
    float v = 0.7f + 0.45f * noise(float(x), float(y), 8, 141);
    if (rnd(x, y, 142) > 0.85f) v += 0.15f;
    return grey(v);
}
Texel bark(int x, int y) { return grey(0.85f + 0.25f * noise(float(x) , float(y) * 0.2f, 8, 151) + 0.05f * rnd(x, y, 152)); }
Texel windows(int x, int y) {
    const int cx = x % (8 * S), cy = y % (8 * S);
    if (cx >= 2 * S && cx < 6 * S && cy >= 2 * S && cy < 7 * S) {
        const bool lit = rnd(x / (8 * S), y / (8 * S), 161) > 0.45f;
        return lit ? Texel{1.9f, 1.7f, 1.1f} : grey(0.45f);
    }
    return grey(0.9f + 0.05f * rnd(x / S, y / S, 162));
}
Texel tread(int x, int y) { return grey(((y + (x / (8 * S)) * 2 * S) % (6 * S)) < 2 * S ? 0.6f : 1.05f); }
Texel cloud(int x, int y) { return grey(0.95f + 0.08f * fbm(float(x), float(y), 181)); }

constexpr Fn kFns[TexCount] = {white,  asphalt, grass, dirt,   sand,   snow, ice,     water, brick, panel,
                               wood,   rock,    arrows, stripes, leaves, bark, windows, tread, cloud};

uint8_t to_byte(float v) { return uint8_t(std::clamp(v * 127.5f, 0.0f, 255.0f)); }

} // namespace

std::vector<uint8_t> make_texture_layers() {
    std::vector<uint8_t> out;
    for (int layer = 0; layer < int(TexCount); ++layer) {
        std::vector<float> img(size_t(N * N * 3));
        for (int y = 0; y < N; ++y)
            for (int x = 0; x < N; ++x) {
                const Texel t = kFns[layer](x, y);
                float* p = &img[size_t((y * N + x) * 3)];
                p[0] = t.r, p[1] = t.g, p[2] = t.b;
            }
        int size = N;
        for (int mip = 0; mip < kTexMips; ++mip) {
            for (int i = 0; i < size * size; ++i) {
                for (int c = 0; c < 3; ++c) out.push_back(to_byte(img[size_t(i * 3 + c)]));
                out.push_back(255);
            }
            if (size == 1) break;
            // Box-filter down a level.
            const int half = size / 2;
            std::vector<float> next(size_t(half * half * 3));
            for (int y = 0; y < half; ++y)
                for (int x = 0; x < half; ++x)
                    for (int c = 0; c < 3; ++c)
                        next[size_t((y * half + x) * 3 + c)] =
                            0.25f * (img[size_t(((2 * y) * size + 2 * x) * 3 + c)] + img[size_t(((2 * y) * size + 2 * x + 1) * 3 + c)] +
                                     img[size_t(((2 * y + 1) * size + 2 * x) * 3 + c)] +
                                     img[size_t(((2 * y + 1) * size + 2 * x + 1) * 3 + c)]);
            img = std::move(next);
            size = half;
        }
    }
    return out;
}

} // namespace gk::render
