#pragma once
#include "render/renderer.h"

#include "gk/sim/world.h"

#include <string>

namespace gk {

struct HudMessage {
    std::string text;
    uint32_t color = 0xffffffffu;
    float time_left = 0.0f;
};

// Race HUD drawn in internal-resolution pixels: lap, time, place, speed,
// coins, mini-turbo charge, countdown and results.
void draw_hud(render::Renderer& r, const sim::World& world, int player, const HudMessage& message, bool show_help);

std::string format_race_time(int ticks);
const char* ordinal_suffix(int place);

} // namespace gk
