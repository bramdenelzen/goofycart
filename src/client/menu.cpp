#include "menu.h"

#include <algorithm>
#include <cstdio>

namespace gk {
using render::rgba;

namespace {

int& part_index(sim::Build& b, int row) {
    switch (row) {
    case 0: return b.driver;
    case 1: return b.body;
    case 2: return b.tires;
    default: return b.glider;
    }
}

const std::vector<sim::Part>& part_list(const sim::PartsCatalog& p, int row) {
    switch (row) {
    case 0: return p.drivers;
    case 1: return p.bodies;
    case 2: return p.tires;
    default: return p.gliders;
    }
}

} // namespace

SelectAction update_select(SelectState& s, const ClientActions& act, const sim::PartsCatalog& parts) {
    if (act.back) return SelectAction::Back;
    if (act.accept) return SelectAction::Accept;
    if (act.menu_up) s.row = (s.row + 3) % 4;
    if (act.menu_down) s.row = (s.row + 1) % 4;
    const int dir = (act.menu_right ? 1 : 0) - (act.menu_left ? 1 : 0);
    if (dir != 0) {
        const int n = int(part_list(parts, s.row).size());
        if (n > 0) {
            int& i = part_index(s.build, s.row);
            i = (i + dir + n) % n;
            return SelectAction::Changed;
        }
    }
    return SelectAction::None;
}

void draw_select(render::Renderer& r, const SelectState& s, const sim::PartsCatalog& parts,
                 const net::LobbyMsg* lobby, const std::string& track_name) {
    const VkExtent2D ext = r.internal_extent();
    const float W = float(ext.width), H = float(ext.height);
    char buf[96];

    // Part pickers
    const float x = 12.0f;
    float y = 12.0f;
    r.ui_rect(x - 6, y - 6, 244, 190, rgba(0, 0, 0, 140));
    r.ui_text(x, y, "CHOOSE YOUR KART", rgba(255, 230, 60), 2.0f);
    y += 26.0f;
    static const char* kLabels[4] = {"DRIVER", "BODY", "TIRES", "GLIDER"};
    for (int row = 0; row < 4; ++row) {
        sim::Build b = s.build;
        const auto& list = part_list(parts, row);
        const int i = std::clamp(part_index(b, row), 0, int(list.size()) - 1);
        const bool sel = row == s.row;
        if (sel) r.ui_rect(x - 3, y - 3, 238, 15, rgba(255, 200, 40, 80));
        r.ui_text(x, y, kLabels[row], sel ? rgba(255, 230, 60) : rgba(200, 200, 200), 1.0f);
        std::snprintf(buf, sizeof buf, "< %s >", list.empty() ? "-" : list[size_t(i)].name.c_str());
        r.ui_text(x + 60, y, buf, rgba(255, 255, 255), 1.0f);
        y += 16.0f;
    }

    // Stat bars, as on the MK8 select screen: (Level + 3) / 4, out of 5.75.
    y += 6.0f;
    const sim::StatArray lv = parts.levels(s.build);
    struct Bar {
        const char* name;
        sim::Stat stat;
    };
    static const Bar kBars[] = {
        {"SPEED", sim::Stat::SpeedGround},    {"ACCEL", sim::Stat::Acceleration}, {"WEIGHT", sim::Stat::Weight},
        {"HANDLING", sim::Stat::HandlingGround}, {"TRACTION", sim::Stat::Traction}, {"MINI-TURBO", sim::Stat::MiniTurbo},
    };
    for (const Bar& bar : kBars) {
        const float v = sim::menu_value(lv[size_t(bar.stat)]);
        r.ui_text(x, y, bar.name, rgba(200, 200, 200), 1.0f);
        const float bx = x + 72, bw = 120.0f;
        r.ui_rect(bx, y, bw, 6, rgba(255, 255, 255, 40));
        r.ui_rect(bx, y, bw * std::clamp(v / 5.75f, 0.0f, 1.0f), 6, rgba(90, 200, 255));
        std::snprintf(buf, sizeof buf, "%.2f", double(v));
        r.ui_text(bx + bw + 4, y, buf, rgba(200, 200, 200), 1.0f);
        y += 12.0f;
    }

    // Lobby
    if (lobby) {
        const float lx = W - 200.0f;
        float ly = 12.0f;
        r.ui_rect(lx - 6, ly - 6, 194, 40.0f + float(lobby->players.size()) * 12.0f + 8, rgba(0, 0, 0, 140));
        std::snprintf(buf, sizeof buf, "RACE %d OF %d", lobby->race_number, lobby->races_per_gp);
        r.ui_text(lx, ly, buf, rgba(255, 230, 60), 1.5f);
        ly += 16.0f;
        if (lobby->seconds_left > 0) std::snprintf(buf, sizeof buf, "STARTS IN %d", lobby->seconds_left);
        else std::snprintf(buf, sizeof buf, "WAITING FOR PLAYERS");
        r.ui_text(lx, ly, buf, rgba(200, 230, 255), 1.0f);
        ly += 16.0f;
        for (const auto& p : lobby->players) {
            std::snprintf(buf, sizeof buf, "%-14s %3d PTS  %s", p.name.substr(0, 14).c_str(), p.points,
                          p.ready ? "READY" : "...");
            r.ui_text(lx, ly, buf, p.you ? rgba(255, 230, 60) : rgba(255, 255, 255), 1.0f);
            ly += 12.0f;
        }
    }

    if (!track_name.empty()) {
        const float tw = render::Renderer::ui_text_width(track_name.c_str(), 1.5f);
        r.ui_rect(W - tw - 18, H - 52, tw + 12, 18, rgba(0, 0, 0, 140));
        r.ui_text(W - tw - 12, H - 48, track_name.c_str(), rgba(255, 230, 60), 1.5f);
    }
    const char* hint = !lobby ? "ENTER / A: RACE     ESC: BACK"
                     : s.ready ? "READY!   ENTER / A: CHANGE YOUR MIND"
                               : "ENTER / A: READY     ESC: QUIT";
    const float hw = render::Renderer::ui_text_width(hint, 1.5f);
    r.ui_rect((W - hw) * 0.5f - 6, H - 26, hw + 12, 18, rgba(0, 0, 0, 140));
    r.ui_text((W - hw) * 0.5f, H - 22, hint, rgba(255, 255, 255), 1.5f);
    r.ui_text(x, H - 36, "UP/DOWN: PART   LEFT/RIGHT: CHANGE", rgba(200, 200, 200), 1.0f);
}

} // namespace gk
