#pragma once
#include "render/renderer.h"

#include "gk/sim/world.h"

#include <string>
#include <vector>

namespace gk {

struct HudMessage {
    std::string text;
    uint32_t color = 0xffffffffu;
    float time_left = 0.0f;
};

struct ResultRow {
    std::string name;
    int finish_ticks = 0; // 0: did not finish
    int points = 0;       // this race, -1 to hide
    int total = -1;       // Grand Prix total, -1 to hide
    bool highlight = false;
};

// Race HUD drawn in internal-resolution pixels: lap, time, place, speed,
// coins, mini-turbo charge and countdown. When the player has finished and
// `finish_table` is set, also the live finishing order.
void draw_hud(render::Renderer& r, const sim::World& world, int player, const std::vector<std::string>& names,
              const HudMessage& message, bool show_help, bool finish_table);

// A results table centred on screen.
void draw_results(render::Renderer& r, const std::string& title, const std::vector<ResultRow>& rows,
                  const std::string& footer = {});

std::string format_race_time(int ticks);
const char* ordinal_suffix(int place);

} // namespace gk
