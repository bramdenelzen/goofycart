#pragma once
// Painted skyboxes: an equirectangular image per theme with a sky gradient,
// clouds and a distant backdrop (hills, mesas, peaks, a city), drawn once
// on the CPU and sampled by direction in mesh.frag. The sun is added there.
#include "gk/core/math.h"

#include <cstdint>
#include <string>
#include <vector>

namespace gk::render {

struct SkyImage {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;
    vec3 zenith{0.2f, 0.5f, 0.9f};   // for sky light from above
    vec3 horizon{0.8f, 0.9f, 1.0f};  // the haze distant things fade into
};

// Rows: v = 0 is straight up, v = kSkyHorizonV the horizon, v = 1 a little
// below it. Columns: u = 0..1 all the way round.
inline constexpr float kSkyHorizonV = 0.88f;
SkyImage paint_sky(const std::string& theme, int width = 1024, int height = 512);

} // namespace gk::render
