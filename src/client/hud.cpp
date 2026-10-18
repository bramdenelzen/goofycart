#include "hud.h"

#include "ui_style.h"

#include "gk/core/fixed_step.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace gk {
using render::rgba;

static constexpr int kRacePoints[12] = {15, 12, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1};

uint32_t kart_color(int slot) {
    static constexpr uint32_t kColors[12] = {
        rgba(220, 50, 40),  rgba(40, 110, 230), rgba(60, 190, 70),  rgba(250, 200, 30),
        rgba(160, 70, 210), rgba(250, 130, 30), rgba(30, 200, 200), rgba(240, 100, 170),
        rgba(240, 240, 240), rgba(70, 70, 80),  rgba(150, 100, 60), rgba(120, 200, 255),
    };
    return kColors[size_t(((slot % 12) + 12) % 12)];
}

void draw_minimap(render::Renderer& r, const sim::World& w, int player) {
    const sim::Track& t = w.track();
    const int n = int(t.center.size());
    if (n < 4) return;
    const VkExtent2D ext = r.ui_extent();
    const float H = float(ext.height);
    // Fit the track into a box on the left, below the lap and time, seen
    // from above (+x right, +z down) and kept in proportion.
    vec2 lo(1e9f), hi(-1e9f);
    float y_lo = 1e9f, y_hi = -1e9f;
    for (const auto& c : t.center) {
        lo = glm::min(lo, vec2(c.pos.x, c.pos.z)), hi = glm::max(hi, vec2(c.pos.x, c.pos.z));
        y_lo = std::min(y_lo, c.pos.y), y_hi = std::max(y_hi, c.pos.y);
    }
    const float box = std::min(H * 0.34f, 120.0f), x0 = 8.0f, y0 = 50.0f;
    const vec2 span = glm::max(hi - lo, vec2(1.0f));
    const float scale = box / std::max(span.x, span.y);
    const vec2 origin = vec2(x0, y0) + (vec2(box) - span * scale) * 0.5f;
    auto to_map = [&](vec3 p) { return origin + (vec2(p.x, p.z) - lo) * scale; };

    // The road, lowest parts first so bridges are drawn over what they cross:
    // a dark outline pass, then the road shaded lighter with height.
    std::vector<int> order(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) order[size_t(i)] = i;
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        return t.center[size_t(a)].pos.y + t.center[size_t(t.wrap(a + 1))].pos.y <
               t.center[size_t(b)].pos.y + t.center[size_t(t.wrap(b + 1))].pos.y;
    });
    const float yspan = std::max(y_hi - y_lo, 1.0f);
    for (int pass = 0; pass < 2; ++pass)
        for (int i : order) {
            const sim::CenterSample &a = t.center[size_t(i)], &b = t.center[size_t(t.wrap(i + 1))];
            if (!a.has_floor || !b.has_floor) continue;
            const float half = std::max(1.2f, a.half_width * scale) + (pass == 0 ? 1.0f : 0.0f);
            const vec2 pa = to_map(a.pos), pb = to_map(b.pos);
            vec2 d = pb - pa;
            const float len = glm::length(d);
            if (len < 1e-4f) continue;
            d /= len;
            const vec2 across(-d.y * half, d.x * half), ext_d = d * (pass == 0 ? 1.0f : 0.5f);
            const uint8_t v = uint8_t(150.0f + 80.0f * (0.5f * (a.pos.y + b.pos.y) - y_lo) / yspan);
            const uint32_t color = pass == 0 ? rgba(10, 10, 20, 170) : rgba(v, v, uint8_t(std::min(255, v + 15)), 230);
            r.ui_quad(pa - ext_d - across, pb + ext_d - across, pb + ext_d + across, pa - ext_d + across, color);
        }
    // Start line.
    {
        const sim::CenterSample& s = t.center[0];
        const vec2 p = to_map(s.pos);
        const vec2 rt = glm::normalize(vec2(s.right.x, s.right.z)) * std::max(2.0f, s.half_width * scale + 1.0f);
        const vec2 fw = glm::normalize(vec2(s.tangent.x, s.tangent.z)) * 0.8f;
        r.ui_quad(p - rt - fw, p - rt + fw, p + rt + fw, p + rt - fw, rgba(255, 255, 255));
    }
    // Karts: everyone else, then the player on top with an outline, pointing
    // the way they face.
    const auto& karts = w.karts();
    for (int pass = 0; pass < 2; ++pass)
        for (size_t i = 0; i < karts.size(); ++i) {
            const bool me = int(i) == player;
            if (me != (pass == 1)) continue;
            const vec2 p = to_map(karts[i].pos);
            const float s = me ? 3.0f : 2.0f;
            if (me) {
                vec3 f = karts[i].forward();
                vec2 dir(f.x, f.z);
                dir = glm::length(dir) > 1e-3f ? glm::normalize(dir) : vec2(0, 1);
                const vec2 side(-dir.y, dir.x);
                const vec2 tip = p + dir * 7.0f, l = p - dir * 3.0f + side * 4.0f, rr = p - dir * 3.0f - side * 4.0f;
                r.ui_quad(tip + dir, l - dir * 0.5f + side, p - dir, rr - dir * 0.5f - side, rgba(0, 0, 0, 220));
                r.ui_quad(tip, l, p - dir * 1.5f, rr, rgba(255, 255, 255));
            } else {
                r.ui_rect(p.x - s - 1, p.y - s - 1, 2 * s + 2, 2 * s + 2, rgba(0, 0, 0, 200));
                r.ui_rect(p.x - s, p.y - s, 2 * s, 2 * s, kart_color(int(i)));
            }
        }
}

std::string format_race_time(int ticks) {
    const int ms_total = int(std::lround(double(ticks) * 1000.0 / kTickRate));
    char buf[32];
    std::snprintf(buf, sizeof buf, "%d:%02d.%03d", ms_total / 60000, (ms_total / 1000) % 60, ms_total % 1000);
    return buf;
}

const char* ordinal_suffix(int place) {
    if (place % 100 >= 11 && place % 100 <= 13) return "th";
    switch (place % 10) {
    case 1: return "st";
    case 2: return "nd";
    case 3: return "rd";
    default: return "th";
    }
}

namespace {
uint32_t tier_color(int tier) {
    switch (tier) {
    case 1: return rgba(80, 160, 255);
    case 2: return rgba(255, 150, 40);
    case 3: return rgba(200, 90, 255);
    default: return rgba(200, 200, 200);
    }
}

void centered(render::Renderer& r, float cx, float y, const char* text, uint32_t color, float scale) {
    r.ui_text(cx - render::Renderer::ui_text_width(text, scale) * 0.5f, y, text, color, scale);
}
} // namespace

namespace {
// Short label and panel colour for the item slots.
struct ItemLook {
    const char* label;
    uint32_t color;
};
ItemLook item_look(sim::Item item) {
    using sim::Item;
    switch (item) {
    case Item::Banana: case Item::TripleBanana: return {"BANANA", rgba(230, 200, 40)};
    case Item::GreenShell: case Item::TripleGreen: return {"GREEN", rgba(50, 170, 60)};
    case Item::RedShell: case Item::TripleRed: return {"RED", rgba(210, 50, 40)};
    case Item::SpikedShell: return {"SPIKED", rgba(60, 100, 230)};
    case Item::Bomb: return {"BOMB", rgba(50, 50, 60)};
    case Item::Mushroom: case Item::TripleMushroom: return {"SHROOM", rgba(220, 70, 60)};
    case Item::GoldenMushroom: return {"GOLDEN", rgba(230, 180, 30)};
    case Item::Bullet: return {"BULLET", rgba(40, 40, 50)};
    case Item::Ink: return {"INK", rgba(60, 40, 90)};
    case Item::Lightning: return {"ZAP", rgba(240, 220, 60)};
    case Item::Star: return {"STAR", rgba(250, 210, 40)};
    case Item::Fireball: return {"FIRE", rgba(240, 110, 30)};
    case Item::Boomerang: return {"BOOMER", rgba(70, 130, 240)};
    case Item::Chomper: return {"CHOMP", rgba(60, 160, 60)};
    case Item::Horn: return {"HORN", rgba(200, 160, 60)};
    case Item::LuckyEight: return {"LUCKY 8", rgba(160, 80, 200)};
    case Item::Coin: return {"COIN", rgba(240, 200, 40)};
    case Item::Ghost: return {"GHOST", rgba(200, 200, 220)};
    default: return {"", rgba(0, 0, 0)};
    }
}

void item_slot(render::Renderer& r, float x, float y, float size, sim::Item item, int uses, bool spinning, uint64_t tick) {
    // A hard-edged frame: black shadow, white rim, a black window.
    r.ui_rect(x + 3.0f, y + 3.0f, size, size, rgba(0, 0, 0, 140));
    r.ui_rect(x - 2.5f, y - 2.5f, size + 5.0f, size + 5.0f, rgba(240, 240, 245));
    r.ui_round_rect(x, y, size, size, 0.0f, rgba(30, 30, 38, 240), rgba(6, 6, 10, 245));
    r.ui_rect(x - 2.5f, y - 2.5f, size + 5.0f, 2.5f, ui::kRed);
    if (spinning) item = sim::Item(1 + (tick / 4) % uint64_t(sim::kItemCount));
    if (item == sim::Item::None) return;
    const ItemLook look = item_look(item);
    const uint32_t c = look.color & 0x00ffffffu;
    r.ui_round_rect(x + 3.0f, y + 3.0f, size - 6.0f, size - 6.0f, 0.0f, c | 0xf0000000u, c | 0xa0000000u);
    r.ui_rect(x + 3.0f, y + 3.0f, size - 6.0f, (size - 6.0f) * 0.35f, rgba(255, 255, 255, 50));
    const float scale = size > 40 ? 1.0f : 0.75f;
    centered(r, x + size * 0.5f, y + size * 0.5f - 3.5f * scale, look.label, rgba(255, 255, 255), scale);
    if (uses > 1 && !spinning) {
        char buf[8];
        std::snprintf(buf, sizeof buf, "x%d", uses);
        ui::badge(r, x + size - 22.0f, y + size - 8.0f, 26.0f, 12.0f, ui::kRed, buf, ui::kText, 1.0f);
    }
}
} // namespace

void draw_results(render::Renderer& r, const std::string& title, const std::vector<ResultRow>& rows,
                  const std::string& footer) {
    const float W = ui::W(r), H = ui::H(r);
    const bool totals = !rows.empty() && rows[0].total >= 0;
    const float tw = totals ? 300.0f : 260.0f, rh = 14.0f;
    const float x = W * 0.5f - tw * 0.5f;
    float y = H * 0.08f;
    ui::title(r, W * 0.5f, y, title.c_str(), 3.2f);
    y += 34.0f;
    ui::panel(r, x - 10.0f, y - 8.0f, tw + 20.0f, float(rows.size()) * rh + 14.0f, 12.0f);
    char buf[128];
    for (size_t i = 0; i < rows.size(); ++i) {
        const ResultRow& row = rows[i];
        const std::string time = row.finish_ticks > 0 ? format_race_time(row.finish_ticks) : std::string("--:--.---");
        const std::string name = row.name.substr(0, 14);
        if (row.highlight) ui::pill(r, x - 4.0f, y - 2.5f, tw + 8.0f, rh - 1.0f, true);
        else if (i % 2) r.ui_slant_rect(x - 4.0f, y - 2.5f, tw + 8.0f, rh - 1.0f, ui::kBarSlant, rgba(255, 255, 255, 14), rgba(255, 255, 255, 14));
        auto put = [&](float px, const char* t, uint32_t c) { r.ui_text(px, y, t, row.highlight ? ui::kText : c, 1.1f); };
        // Place badges: gold, silver, bronze.
        const uint32_t medal = i == 0 ? ui::kGold : i == 1 ? rgba(215, 225, 240) : i == 2 ? rgba(230, 150, 90) : rgba(255, 255, 255, 60);
        std::snprintf(buf, sizeof buf, "%zu%s", i + 1, ordinal_suffix(int(i) + 1));
        ui::badge(r, x, y - 1.5f, 26.0f, 11.0f, medal, buf, i < 3 ? ui::kInk : ui::kText, 0.9f);
        put(x + 32.0f, name.c_str(), ui::kText);
        put(x + 130.0f, time.c_str(), ui::kSubtle);
        if (row.points >= 0) {
            std::snprintf(buf, sizeof buf, "+%d", row.points);
            put(x + 200.0f, buf, ui::kGold);
        }
        if (totals) {
            std::snprintf(buf, sizeof buf, "%d", row.total);
            put(x + tw - render::Renderer::ui_text_width(buf, 1.1f), buf, ui::kText);
        }
        y += rh;
    }
    if (!footer.empty()) ui::footer(r, footer.c_str());
}

void draw_hud(render::Renderer& r, const sim::World& w, int player, const std::vector<std::string>& names,
              const HudMessage& msg, bool show_help, bool finish_table) {
    const VkExtent2D ext = r.ui_extent();
    const float W = float(ext.width), H = float(ext.height);
    const sim::KartState& k = w.karts()[size_t(player)];
    const auto& kc = w.tables().constants;
    char buf[96];

    // Lap and time, top left
    const int lap = std::min(k.lap, w.config().laps);
    {
        // Arcade style: labels in yellow, figures big and white, no boxes.
        const int ticks = k.finished ? k.finish_tick : w.race_ticks();
        r.ui_slant_rect(4, 8, 150, 44, ui::kSlant, rgba(0, 0, 0, 110), rgba(0, 0, 0, 0));
        r.ui_text(12, 12, "TIME", ui::kGold, 1.0f);
        r.ui_text(46, 10, format_race_time(ticks).c_str(), ui::kText, 1.6f);
        r.ui_text(12, 32, "LAP", ui::kGold, 1.0f);
        std::snprintf(buf, sizeof buf, "%d", lap);
        ui::heading(r, 44, 28, buf, 2.2f, ui::kText, rgba(200, 205, 215));
        std::snprintf(buf, sizeof buf, "/%d", w.config().laps);
        r.ui_text(46 + render::Renderer::ui_text_width(std::to_string(lap).c_str(), 2.2f), 35, buf, ui::kSubtle, 1.3f);
    }
    // Place, top right
    const int place = w.place_of(player);
    std::snprintf(buf, sizeof buf, "%d%s", place, ordinal_suffix(place));
    {
        // Gold, silver and bronze for the podium; the suffix rides small.
        const uint32_t pc = place == 1 ? ui::kGoldDeep : place == 2 ? rgba(150, 165, 185) : place == 3 ? rgba(200, 105, 50) : rgba(225, 20, 30);
        char num[8];
        std::snprintf(num, sizeof num, "%d", place);
        const float nw = render::Renderer::ui_text_width(num, 6.0f), sw = render::Renderer::ui_text_width(ordinal_suffix(place), 2.6f);
        const float px = W - nw - sw - 14.0f;
        ui::heading(r, px - 6.0f, 14, num, 6.0f, place == 1 ? rgba(255, 245, 140) : rgba(255, 255, 255), pc);
        ui::heading(r, px + nw - 2.0f, 18, ordinal_suffix(place), 2.4f, rgba(255, 255, 255), pc);
    }

    // Item slots, top centre: the item in hand and the reserve.
    {
        const float big = 44.0f, small = 28.0f;
        const float x = W * 0.5f - big * 0.5f + small * 0.5f;
        const bool spin_main = k.roulette > 0 && k.item == sim::Item::None;
        const bool spin_next = k.roulette > 0 && k.item != sim::Item::None;
        item_slot(r, x - small - 8, 6, small, k.item_next, 1, spin_next, w.tick_count());
        item_slot(r, x, 6, big, k.item, k.item_uses, spin_main, w.tick_count());
        if (k.golden_frames > 0) centered(r, x + big * 0.5f, 56, "GOLDEN! TAP ITEM", rgba(255, 220, 60), 1.0f);
        if (k.trailing) centered(r, x + big * 0.5f, 56, "HOLDING", rgba(200, 230, 255), 1.0f);
    }
    // Ink splats: they fade as the timer runs out.
    if (k.ink_frames > 0) {
        const uint8_t a = uint8_t(std::min(230, k.ink_frames * 3));
        const float spots[6][3] = {{0.3f, 0.35f, 0.16f}, {0.62f, 0.3f, 0.2f}, {0.45f, 0.6f, 0.14f},
                                   {0.75f, 0.62f, 0.12f}, {0.18f, 0.6f, 0.1f}, {0.52f, 0.45f, 0.1f}};
        for (const auto& sp : spots) {
            const float rad = sp[2] * H;
            r.ui_rect(W * sp[0] - rad, H * sp[1] - rad * 0.7f, rad * 2.0f, rad * 1.4f, rgba(15, 10, 25, a));
            r.ui_rect(W * sp[0] - rad * 0.7f, H * sp[1] - rad, rad * 1.4f, rad * 2.0f, rgba(15, 10, 25, a));
        }
    }

    draw_minimap(r, w, player);

    // Speed and coins, bottom left
    const float kmh = std::abs(glm::length(k.velocity())) * 3.6f;
    {
        // Speed in big italic figures, coins beside it.
        r.ui_slant_rect(4, H - 46, 170, 40, ui::kSlant, rgba(0, 0, 0, 0), rgba(0, 0, 0, 120));
        std::snprintf(buf, sizeof buf, "%03.0f", double(kmh));
        ui::heading(r, 12, H - 40, buf, 3.0f, ui::kText, rgba(200, 205, 215));
        r.ui_text(14 + render::Renderer::ui_text_width(buf, 3.0f), H - 25, "KM/H", ui::kGold, 1.0f);
        r.ui_round_rect(112, H - 39, 14, 14, 7, rgba(255, 236, 120), rgba(230, 150, 10));
        std::snprintf(buf, sizeof buf, "x%d", k.coins);
        r.ui_text(130, H - 37, buf, k.coins >= 10 ? ui::kGold : ui::kText, 1.5f);
    }
    // Mini-turbo charge bar, bottom centre
    if (k.drift == sim::DriftState::Drifting) {
        const float bw = 140.0f, bh = 8.0f, bx = W * 0.5f - bw * 0.5f, by = H - 64.0f;
        const float full = kc.mt_thresholds[2];
        r.ui_slant_rect(bx - 3, by - 2, bw + 6, bh + 4, ui::kBarSlant, rgba(0, 0, 0, 200), rgba(0, 0, 0, 200));
        const float fill = std::min(1.0f, k.mt_charge / full);
        const uint32_t tc = tier_color(k.mt_tier(kc));
        const int segs = 28, lit = int(float(segs) * fill);
        for (int sgm = 0; sgm < lit; ++sgm)
            r.ui_slant_rect(bx + float(sgm) * (bw / float(segs)), by, bw / float(segs) - 1.0f, bh, ui::kBarSlant, tc | 0xff000000u,
                            (tc & 0x00ffffffu) | 0xb0000000u);
        for (int i = 0; i < 2; ++i) r.ui_rect(bx + bw * kc.mt_thresholds[size_t(i)] / full, by, 1, bh, rgba(255, 255, 255, 200));
    }
    if (k.boost_frames > 0) {
        const char* label = "BOOST";
        switch (k.boost) {
        case sim::BoostKind::MiniTurbo: label = "MINI-TURBO"; break;
        case sim::BoostKind::SuperMiniTurbo: label = "SUPER MINI-TURBO"; break;
        case sim::BoostKind::UltraMiniTurbo: label = "ULTRA MINI-TURBO"; break;
        case sim::BoostKind::Trick: label = "TRICK BOOST"; break;
        case sim::BoostKind::RocketStart: label = "ROCKET START"; break;
        case sim::BoostKind::GlideRamp: label = "GLIDE RAMP"; break;
        case sim::BoostKind::BoostPad: label = "DASH PANEL"; break;
        default: break;
        }
        ui::title(r, W * 0.5f, H * 0.62f, label, 1.8f, rgba(255, 200, 80));
    }
    if (k.gliding) centered(r, W * 0.5f, H * 0.25f, "GLIDING  (stick up/down to pitch)", rgba(160, 210, 255), 1.0f);
    if (k.respawn_frames > 0) centered(r, W * 0.5f, H * 0.35f, "RESPAWNING...", rgba(255, 255, 255), 2.0f);

    // Countdown
    const int cd = w.countdown_left();
    if (cd > 0) {
        const int n = (cd + 59) / 60;
        std::snprintf(buf, sizeof buf, "%d", n);
        // Each number pops in big and settles.
        const float t = float((cd - 1) % 60) / 60.0f;
        const float s = 9.0f + 3.0f * t * t;
        ui::title(r, W * 0.5f, H * 0.33f - 3.5f * (s - 9.0f), buf, s, n == 1 ? rgba(255, 120, 90) : n == 2 ? ui::kGold : rgba(130, 210, 255));
    } else if (w.race_ticks() < 60) {
        const float s = 9.0f + 2.0f * float(w.race_ticks()) / 60.0f;
        ui::title(r, W * 0.5f, H * 0.33f - 3.5f * (s - 9.0f), "GO!", s, rgba(90, 255, 130));
    }

    if (msg.time_left > 0.0f) ui::title(r, W * 0.5f, cd > 0 || w.race_ticks() < 60 ? H * 0.6f : H * 0.44f, msg.text.c_str(), 2.2f, msg.color);

    // Live finishing order once the player has crossed the line
    if (k.finished && finish_table) {
        std::vector<ResultRow> rows;
        const auto order = w.standings();
        for (size_t i = 0; i < order.size(); ++i) {
            const sim::KartState& o = w.karts()[size_t(order[i])];
            ResultRow row;
            row.name = size_t(order[i]) < names.size() ? names[size_t(order[i])] : "CPU";
            row.finish_ticks = o.finished ? o.finish_tick : 0;
            row.points = order.size() < 2 ? -1 : i < 12 ? kRacePoints[i] : 0; // no points racing alone
            row.highlight = order[i] == player;
            rows.push_back(row);
        }
        draw_results(r, "FINISH!", rows);
    }

    if (show_help) {
        const char* lines[] = {
            "Arrows/WASD steer   X/Space gas   Z brake   Shift hop / drift / trick",
            "C look back   Esc pause   F1 hide help   F2 low-res look",
            "Pad: stick steer, A gas, B brake, R drift, Start pause",
        };
        float lw = 0.0f;
        for (const char* l : lines) lw = std::max(lw, render::Renderer::ui_text_width(l, 0.9f));
        r.ui_slant_rect(W - lw - 26.0f, H - 50.0f, lw + 22.0f, 42.0f, ui::kSlant, rgba(0, 0, 0, 110), rgba(0, 0, 0, 150));
        float y = H - 44.0f;
        for (const char* l : lines) {
            r.ui_text(W - lw - 16.0f, y, l, rgba(230, 232, 240, 230), 0.9f, false);
            y += 11.5f;
        }
    }
}

} // namespace gk
