#include "menu.h"

#include "ui_style.h"

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
    const float W = ui::W(r), H = ui::H(r);
    char buf[96];

    // Part pickers
    const float x = 16.0f, pw = 270.0f, rh = 19.0f;
    float y = 16.0f;
    ui::panel(r, x, y, pw, 206.0f, 14.0f);
    ui::heading(r, x + 14.0f, y + 12.0f, "CHOOSE YOUR KART", 2.2f);
    y += 40.0f;
    static const char* kLabels[4] = {"DRIVER", "BODY", "TIRES", "GLIDER"};
    for (int row = 0; row < 4; ++row) {
        sim::Build b = s.build;
        const auto& list = part_list(parts, row);
        const int i = std::clamp(part_index(b, row), 0, int(list.size()) - 1);
        std::snprintf(buf, sizeof buf, "<  %s  >", list.empty() ? "-" : list[size_t(i)].name.c_str());
        ui::option_row(r, x + 12.0f, y, pw - 24.0f, kLabels[row], buf, row == s.row);
        y += rh;
    }

    // Stat bars, as on the MK8 select screen: (Level + 3) / 4, out of 5.75.
    y += 8.0f;
    const sim::StatArray lv = parts.levels(s.build);
    struct Bar {
        const char* name;
        sim::Stat stat;
        uint32_t color;
    };
    static const Bar kBars[] = {
        {"SPEED", sim::Stat::SpeedGround, render::rgba(255, 120, 90)},       {"ACCEL", sim::Stat::Acceleration, render::rgba(255, 200, 60)},
        {"WEIGHT", sim::Stat::Weight, render::rgba(170, 140, 255)},          {"HANDLING", sim::Stat::HandlingGround, render::rgba(90, 210, 255)},
        {"TRACTION", sim::Stat::Traction, render::rgba(110, 230, 140)},      {"MINI-TURBO", sim::Stat::MiniTurbo, render::rgba(255, 150, 220)},
    };
    for (const Bar& bar : kBars) {
        const float v = sim::menu_value(lv[size_t(bar.stat)]);
        r.ui_text(x + 22.0f, y, bar.name, ui::kSubtle, 1.0f);
        const float bx = x + 104.0f, bw = 140.0f;
        // Segmented gauge, a block per quarter level.
        const int segs = 23, lit = int(std::round(float(segs) * std::clamp(v / 5.75f, 0.0f, 1.0f)));
        for (int sgm = 0; sgm < segs; ++sgm)
            r.ui_slant_rect(bx + float(sgm) * (bw / float(segs)), y, bw / float(segs) - 1.5f, 8.0f, ui::kBarSlant,
                            sgm < lit ? bar.color : rgba(255, 255, 255, 25), sgm < lit ? (bar.color & 0x00ffffffu) | 0xa0000000u : rgba(255, 255, 255, 25));
        y += 13.0f;
    }

    // Lobby
    if (lobby) {
        const float lw = 220.0f, lx = W - lw - 16.0f;
        float ly = 16.0f;
        ui::panel(r, lx, ly, lw, 58.0f + float(lobby->players.size()) * 15.0f, 14.0f);
        std::snprintf(buf, sizeof buf, "RACE %d OF %d", lobby->race_number, lobby->races_per_gp);
        ui::heading(r, lx + 14.0f, ly + 12.0f, buf, 1.8f);
        ly += 32.0f;
        if (lobby->seconds_left > 0) std::snprintf(buf, sizeof buf, "Starts in %d", lobby->seconds_left);
        else std::snprintf(buf, sizeof buf, "Waiting for players");
        r.ui_text(lx + 14.0f, ly, buf, ui::kSubtle, 1.05f);
        ly += 16.0f;
        for (const auto& p : lobby->players) {
            if (p.you) ui::pill(r, lx + 8.0f, ly - 3.0f, lw - 16.0f, 14.0f, true);
            r.ui_text(lx + 16.0f, ly, p.name.substr(0, 14).c_str(), ui::kText, 1.05f);
            std::snprintf(buf, sizeof buf, "%d PTS", p.points);
            r.ui_text(lx + 120.0f, ly, buf, p.you ? ui::kGold : ui::kSubtle, 1.05f);
            ui::badge(r, lx + lw - 60.0f, ly - 1.5f, 46.0f, 11.0f, p.ready ? render::rgba(30, 190, 70) : render::rgba(255, 255, 255, 40),
                      p.ready ? "READY" : "...", ui::kText, 0.8f);
            ly += 15.0f;
        }
    }

    if (!track_name.empty()) {
        const float tw = ui::text_w(track_name.c_str(), 1.6f);
        ui::panel(r, W - tw - 40.0f, H - 64.0f, tw + 24.0f, 24.0f, 12.0f);
        r.ui_text(W - tw - 28.0f, H - 57.0f, track_name.c_str(), ui::kGold, 1.6f);
    }
    const char* hint = !lobby ? "ENTER / A  RACE      ESC  BACK"
                     : s.ready ? "READY!      ENTER / A  CHANGE YOUR MIND"
                               : "UP/DOWN  PART      LEFT/RIGHT  CHANGE      ENTER / A  READY      ESC  LEAVE";
    ui::footer(r, hint);
}

} // namespace gk
