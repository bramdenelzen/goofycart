#include "sky.h"

#include <algorithm>
#include <cmath>

namespace gk::render {
namespace {

float hash3(int x, int y, int z, uint32_t seed) {
    uint32_t h = uint32_t(x) * 374761393u + uint32_t(y) * 668265263u + uint32_t(z) * 2147483647u + seed * 974711u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return float((h ^ (h >> 16)) & 0xffffu) / 65535.0f;
}
float vnoise(vec3 p, uint32_t seed) {
    const vec3 i = glm::floor(p), f = p - i;
    const vec3 u = f * f * (3.0f - 2.0f * f);
    const int x = int(i.x), y = int(i.y), z = int(i.z);
    auto h = [&](int dx, int dy, int dz) { return hash3(x + dx, y + dy, z + dz, seed); };
    const float a = glm::mix(glm::mix(h(0, 0, 0), h(1, 0, 0), u.x), glm::mix(h(0, 1, 0), h(1, 1, 0), u.x), u.y);
    const float b = glm::mix(glm::mix(h(0, 0, 1), h(1, 0, 1), u.x), glm::mix(h(0, 1, 1), h(1, 1, 1), u.x), u.y);
    return glm::mix(a, b, u.z);
}
float fbm(vec3 p, uint32_t seed, int octaves = 5) {
    float s = 0.0f, a = 0.5f, norm = 0.0f;
    for (int o = 0; o < octaves; ++o) {
        s += a * vnoise(p, seed + uint32_t(o) * 31u);
        norm += a;
        p = p * 2.03f + vec3(1.7f, 9.2f, 3.1f);
        a *= 0.5f;
    }
    return s / norm;
}
// Noise along the horizon that wraps all the way round.
float ring(float theta, float freq, uint32_t seed, int octaves = 4) {
    return fbm(vec3(std::cos(theta) * freq, std::sin(theta) * freq, 0.5f), seed, octaves);
}

struct Style {
    vec3 zenith, horizon;
    float clouds;        // coverage 0..1
    vec3 cloud_lit, cloud_shade;
    enum class Back { Hills, Mesas, Peaks, Sea, City, Volcanoes, CloudSea, Town } back;
    vec3 far, near;      // backdrop colours
    bool stars = false;
};

Style style_for(const std::string& t) {
    using B = Style::Back;
    if (t == "desert")
        return {{0.10f, 0.52f, 0.86f}, {0.98f, 0.84f, 0.66f}, 0.32f, {1.0f, 0.97f, 0.92f}, {0.86f, 0.74f, 0.66f}, B::Mesas,
                {0.88f, 0.62f, 0.46f}, {0.80f, 0.46f, 0.30f}};
    if (t == "canyon")
        return {{0.12f, 0.50f, 0.84f}, {0.98f, 0.80f, 0.62f}, 0.36f, {1.0f, 0.96f, 0.9f}, {0.84f, 0.72f, 0.66f}, B::Mesas,
                {0.86f, 0.55f, 0.42f}, {0.70f, 0.34f, 0.22f}};
    if (t == "harbor")
        return {{0.06f, 0.48f, 0.90f}, {0.76f, 0.90f, 0.98f}, 0.48f, {1.0f, 1.0f, 1.0f}, {0.70f, 0.78f, 0.90f}, B::Sea,
                {0.50f, 0.66f, 0.70f}, {0.28f, 0.50f, 0.36f}};
    if (t == "beach") // a hot clear day over a turquoise sea, islands on the horizon
        return {{0.05f, 0.46f, 0.92f}, {0.72f, 0.92f, 0.98f}, 0.3f, {1.0f, 1.0f, 1.0f}, {0.74f, 0.82f, 0.92f}, B::Sea,
                {0.36f, 0.72f, 0.74f}, {0.30f, 0.56f, 0.40f}};
    if (t == "snow")
        return {{0.16f, 0.46f, 0.88f}, {0.86f, 0.91f, 0.98f}, 0.38f, {1.0f, 1.0f, 1.0f}, {0.74f, 0.80f, 0.90f}, B::Peaks,
                {0.66f, 0.74f, 0.88f}, {0.48f, 0.56f, 0.70f}};
    if (t == "volcano")
        return {{0.16f, 0.05f, 0.06f}, {0.70f, 0.28f, 0.12f}, 0.7f, {0.55f, 0.32f, 0.26f}, {0.20f, 0.12f, 0.12f}, B::Volcanoes,
                {0.30f, 0.16f, 0.14f}, {0.12f, 0.07f, 0.07f}};
    if (t == "sky")
        return {{0.12f, 0.44f, 0.95f}, {0.86f, 0.93f, 1.0f}, 0.5f, {1.0f, 1.0f, 1.0f}, {0.74f, 0.80f, 0.94f}, B::CloudSea,
                {0.95f, 0.97f, 1.0f}, {0.85f, 0.89f, 0.98f}};
    if (t == "night")
        return {{0.015f, 0.02f, 0.07f}, {0.14f, 0.10f, 0.26f}, 0.12f, {0.25f, 0.24f, 0.36f}, {0.08f, 0.08f, 0.14f}, B::City,
                {0.10f, 0.09f, 0.18f}, {0.05f, 0.05f, 0.10f}, true};
    if (t == "rainbow") // dusk above the clouds: violet overhead, a glowing coral horizon, early stars
        return {{0.09f, 0.04f, 0.24f}, {0.98f, 0.58f, 0.48f}, 0.3f, {1.0f, 0.78f, 0.7f}, {0.42f, 0.28f, 0.46f}, B::CloudSea,
                {0.95f, 0.72f, 0.72f}, {0.78f, 0.55f, 0.66f}, true};
    if (t == "market")
        return {{0.10f, 0.54f, 0.92f}, {0.84f, 0.91f, 0.96f}, 0.42f, {1.0f, 1.0f, 0.98f}, {0.76f, 0.80f, 0.90f}, B::Town,
                {0.56f, 0.66f, 0.72f}, {0.42f, 0.58f, 0.40f}};
    return {{0.10f, 0.52f, 0.92f}, {0.74f, 0.89f, 0.97f}, 0.55f, {1.0f, 1.0f, 1.0f}, {0.72f, 0.79f, 0.90f}, B::Hills,
            {0.50f, 0.66f, 0.68f}, {0.36f, 0.58f, 0.32f}};
}

// Height of a backdrop layer (radians above the horizon) at azimuth theta.
float backdrop(Style::Back b, float theta, int layer, uint32_t seed, bool* cap, float* window) {
    *cap = false;
    *window = 0.0f;
    const uint32_t s = seed + uint32_t(layer) * 101u;
    const float deg = 3.14159265f / 180.0f;
    switch (b) {
    case Style::Back::Hills: case Style::Back::Town: {
        const float n = ring(theta, layer == 0 ? 2.0f : 3.5f, s);
        return (layer == 0 ? 2.5f + 7.0f * n : 0.8f + 5.0f * n * n) * deg;
    }
    case Style::Back::Mesas: {
        // Flat tops with steep sides: quantised noise, softened at the edges.
        const float n = ring(theta, layer == 0 ? 3.0f : 4.5f, s, 3);
        const float top = glm::smoothstep(0.48f, 0.52f, n);
        const float terrace = glm::smoothstep(0.58f, 0.6f, n);
        return (layer == 0 ? 1.5f + 7.0f * top + 2.5f * terrace : 0.5f + 5.0f * top) * deg;
    }
    case Style::Back::Peaks: {
        const float n = ring(theta, layer == 0 ? 3.0f : 5.0f, s, 5);
        const float ridge = 1.0f - std::abs(2.0f * n - 1.0f);
        const float h = (layer == 0 ? 2.5f + 9.0f * ridge * ridge : 1.0f + 5.0f * ridge * ridge) * deg;
        *cap = true;
        return h;
    }
    case Style::Back::Sea: {
        if (layer == 1) return 0.25f * deg; // the sea itself, up to the horizon
        const float n = ring(theta, 2.5f, s, 4);
        return std::max(0.0f, (n - 0.55f) * 22.0f) * deg; // islands
    }
    case Style::Back::Volcanoes: {
        const float n = ring(theta, 2.0f, s, 3);
        const float cone = std::max(0.0f, 1.0f - std::abs(std::fmod(theta * (layer == 0 ? 3.0f : 5.0f) / (2.0f * 3.14159f) + n, 1.0f) - 0.5f) * 3.5f);
        return (layer == 0 ? 2.0f + 14.0f * cone : 1.0f + 6.0f * cone * n) * deg;
    }
    case Style::Back::CloudSea:
        return (layer == 0 ? 0.6f + 2.5f * ring(theta, 6.0f, s) : -2.0f) * deg;
    case Style::Back::City: {
        const int slots = layer == 0 ? 220 : 120;
        const int i = int(theta / (2.0f * 3.14159265f) * float(slots));
        const float r = hash3(i, layer, 0, s);
        const float h = layer == 0 ? 2.0f + 9.0f * r * r : 1.0f + 5.0f * r;
        *window = hash3(i, layer, 7, s);
        return h * deg;
    }
    }
    return 0.0f;
}

} // namespace

SkyImage paint_sky(const std::string& theme, int width, int height) {
    SkyImage img;
    img.width = width;
    img.height = height;
    img.rgba.assign(size_t(width) * size_t(height) * 4, 255);
    const Style st = style_for(theme);
    img.zenith = st.zenith;
    img.horizon = st.horizon;
    const uint32_t seed = uint32_t(std::hash<std::string>{}(theme));
    const float pi = 3.14159265f;
    const float hy = kSkyHorizonV * float(height);
    for (int y = 0; y < height; ++y) {
        const float el = (hy - (float(y) + 0.5f)) / hy * (pi * 0.5f); // radians above the horizon
        for (int x = 0; x < width; ++x) {
            const float theta = (float(x) + 0.5f) / float(width) * 2.0f * pi;
            // Sky: horizon haze up into a deep zenith.
            const float up = std::clamp(el / (pi * 0.5f), 0.0f, 1.0f);
            vec3 c = glm::mix(st.horizon, st.zenith, std::pow(up, 0.35f));
            if (st.stars && el > 0.05f) {
                const float s = hash3(int(theta * 400.0f), int(el * 400.0f), 3, seed);
                if (s > 0.9965f) c += vec3(0.8f, 0.8f, 0.9f) * (s - 0.9965f) / 0.0035f;
            }
            // Clouds: a layer seen in perspective, fluffier overhead.
            if (el > 0.01f && st.clouds > 0.0f) {
                // Puffy cumulus: billowed noise, flattened less near the horizon.
                const float d = 1.0f / (std::sin(el) + 0.28f);
                const vec3 p(std::cos(theta) * d * 2.2f, std::sin(theta) * d * 2.2f, el * 3.0f);
                const float base = fbm(p, seed + 5u);
                const float n = base + 0.18f * (fbm(p * 3.1f, seed + 6u, 3) - 0.5f);
                const float cover = glm::smoothstep(1.0f - st.clouds, 1.0f - st.clouds + 0.1f, n);
                if (cover > 0.0f) {
                    // Lit from above: the noise a step nearer the horizon gives the shading.
                    const float below = fbm(p * 1.0f + vec3(std::cos(theta), std::sin(theta), 0.0f) * 0.12f, seed + 5u);
                    const float lit = std::clamp(0.62f + (n - below) * 5.0f + 0.25f * up, 0.0f, 1.0f);
                    vec3 cc = glm::mix(st.cloud_shade, st.cloud_lit, lit);
                    cc = glm::mix(cc, st.horizon, 0.45f * (1.0f - std::min(1.0f, el * 6.0f))); // hazier far away
                    c = glm::mix(c, cc, cover * std::min(1.0f, el * 25.0f));
                }
            }
            // The backdrop: two layers, the far one paler with haze.
            for (int layer = 0; layer < 2; ++layer) {
                bool cap = false;
                float window = 0.0f;
                const float h = backdrop(st.back, theta, layer, seed, &cap, &window);
                if (el > h) continue;
                vec3 bc = layer == 0 ? st.far : st.near;
                bc = glm::mix(bc, st.horizon, layer == 0 ? 0.35f : 0.12f);
                bc *= 0.9f + 0.15f * fbm(vec3(theta * 30.0f, el * 300.0f, float(layer)), seed + 9u, 3); // texture
                if (cap && el > h * 0.72f) bc = glm::mix(bc, vec3(0.96f, 0.98f, 1.0f), layer == 0 ? 0.75f : 0.9f);
                if (st.back == Style::Back::City && window > 0.0f) {
                    const bool lit = hash3(int(theta * 900.0f), int(el * 900.0f), layer, seed) > 0.72f;
                    if (lit && int(theta * 900.0f) % 2 == 0 && int(el * 900.0f) % 3 == 0)
                        bc = window > 0.5f ? vec3(1.0f, 0.85f, 0.55f) : vec3(0.6f, 0.9f, 1.0f);
                }
                c = bc;
            }
            if (el < 0.0f) c = glm::mix(c, st.horizon * 0.85f, std::min(1.0f, -el * 8.0f)); // below: haze
            uint8_t* px = &img.rgba[(size_t(y) * size_t(width) + size_t(x)) * 4];
            for (int k = 0; k < 3; ++k) px[k] = uint8_t(std::clamp(c[k], 0.0f, 1.0f) * 255.0f + 0.5f);
        }
    }
    return img;
}

} // namespace gk::render
