#pragma once
// The lava shader's bubbles, worked out on the CPU too, so the app can throw
// a splash where (and when) a bubble pops. Must match lava_color() in
// shaders/mesh.frag: the same hash, cells, periods and snapped clock.
#include "gk/core/math.h"

#include <cmath>
#include <cstdint>

namespace gk::render {

inline constexpr float kLavaCell = 4.0f; // metres; one bubble at most per cell

inline uint32_t lava_hash(int x, int y, uint32_t salt) {
    uint32_t h = uint32_t(x) * 374761393u + uint32_t(y) * 668265263u + salt * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
inline float lava_rnd(int x, int y, uint32_t salt) { return float(lava_hash(x, y, salt) & 0xffffu) / 65535.0f; }

// The shader's clock for the lava: Renderer::shader_time() snapped to eighths.
inline float lava_clock(float shader_time) { return std::floor(shader_time * 8.0f) / 8.0f; }

struct LavaBubble {
    bool active = false;
    vec2 centre{0.0f}; // world x, z
    float phase = 0.0f; // 0 just born .. 1 about to pop
};
inline LavaBubble lava_bubble(int cx, int cy, float clock) {
    LavaBubble b;
    b.active = lava_rnd(cx, cy, 1) < 0.45f;
    if (!b.active) return b;
    const float period = 2.0f + 3.0f * lava_rnd(cx, cy, 2);
    const float v = clock / period + lava_rnd(cx, cy, 3);
    b.phase = v - std::floor(v);
    b.centre = (vec2(float(cx), float(cy)) + 0.5f + (vec2(lava_rnd(cx, cy, 4), lava_rnd(cx, cy, 5)) - 0.5f) * 0.3f) * kLavaCell;
    return b;
}

// The seams' glow pulse, for the lights along the lava.
inline float lava_pulse(float clock, float phase) { return 0.8f + 0.2f * std::sin(clock * 1.5f + phase); }

} // namespace gk::render
