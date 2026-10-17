#include "hud.h"

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
    const VkExtent2D ext = r.internal_extent();
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
    r.ui_rect(x - 2, y - 2, size + 4, size + 4, rgba(255, 255, 255, 200));
    r.ui_rect(x, y, size, size, rgba(20, 20, 30, 220));
    if (spinning) item = sim::Item(1 + (tick / 4) % uint64_t(sim::kItemCount));
    if (item == sim::Item::None) return;
    const ItemLook look = item_look(item);
    r.ui_rect(x + 2, y + 2, size - 4, size - 4, (look.color & 0x00ffffffu) | 0xd0000000u);
    const float scale = size > 40 ? 1.0f : 0.8f;
    centered(r, x + size * 0.5f, y + size * 0.5f - 3.0f * scale, look.label, rgba(255, 255, 255), scale);
    if (uses > 1 && !spinning) {
        char buf[8];
        std::snprintf(buf, sizeof buf, "x%d", uses);
        r.ui_text(x + size - render::Renderer::ui_text_width(buf) - 2, y + size - 9, buf, rgba(255, 255, 255), 1.0f);
    }
}
} // namespace

void draw_results(render::Renderer& r, const std::string& title, const std::vector<ResultRow>& rows,
                  const std::string& footer) {
    const VkExtent2D ext = r.internal_extent();
    const float W = float(ext.width), H = float(ext.height);
    const bool totals = !rows.empty() && rows[0].total >= 0;
    const float tw = totals ? 250.0f : 214.0f;
    const float x = W * 0.5f - tw * 0.5f;
    float y = H * 0.12f;
    centered(r, W * 0.5f, y, title.c_str(), rgba(255, 230, 60), 3.0f);
    y += 30.0f;
    r.ui_rect(x - 6, y - 4, tw + 12, float(rows.size()) * 12.0f + 8, rgba(0, 0, 0, 150));
    char buf[128];
    for (size_t i = 0; i < rows.size(); ++i) {
        const ResultRow& row = rows[i];
        const std::string time = row.finish_ticks > 0 ? format_race_time(row.finish_ticks) : std::string("--:--.---");
        const std::string name = row.name.substr(0, 12);
        // The font is proportional, so each column gets its own x.
        const uint32_t c = row.highlight ? rgba(255, 230, 60) : rgba(255, 255, 255);
        if (row.highlight) r.ui_rect(x - 4, y - 2, tw + 8, 11, rgba(255, 200, 40, 70));
        std::snprintf(buf, sizeof buf, "%zu%s", i + 1, ordinal_suffix(int(i) + 1));
        r.ui_text(x + 22 - render::Renderer::ui_text_width(buf), y, buf, c, 1.0f);
        r.ui_text(x + 30, y, name.c_str(), c, 1.0f);
        r.ui_text(x + 110, y, time.c_str(), c, 1.0f);
        if (row.points >= 0) {
            std::snprintf(buf, sizeof buf, "+%d", row.points);
            r.ui_text(x + 172, y, buf, c, 1.0f);
        }
        if (totals) {
            std::snprintf(buf, sizeof buf, "%d", row.total);
            r.ui_text(x + tw - render::Renderer::ui_text_width(buf), y, buf, c, 1.0f);
        }
        y += 12.0f;
    }
    if (!footer.empty()) centered(r, W * 0.5f, y + 10.0f, footer.c_str(), rgba(200, 230, 255), 1.0f);
}

void draw_hud(render::Renderer& r, const sim::World& w, int player, const std::vector<std::string>& names,
              const HudMessage& msg, bool show_help, bool finish_table) {
    const VkExtent2D ext = r.internal_extent();
    const float W = float(ext.width), H = float(ext.height);
    const sim::KartState& k = w.karts()[size_t(player)];
    const auto& kc = w.tables().constants;
    char buf[96];

    // Lap and time, top left
    const int lap = std::min(k.lap, w.config().laps);
    std::snprintf(buf, sizeof buf, "LAP %d/%d", lap, w.config().laps);
    r.ui_text(8, 8, buf, rgba(255, 255, 255), 2.0f);
    const int ticks = k.finished ? k.finish_tick : w.race_ticks();
    r.ui_text(8, 28, format_race_time(ticks).c_str(), rgba(255, 240, 160), 1.5f);

    // Place, top right
    const int place = w.place_of(player);
    std::snprintf(buf, sizeof buf, "%d%s", place, ordinal_suffix(place));
    const float pw = render::Renderer::ui_text_width(buf, 4.0f);
    r.ui_text(W - pw - 10, 6, buf, place == 1 ? rgba(255, 220, 40) : rgba(255, 255, 255), 4.0f);

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
    std::snprintf(buf, sizeof buf, "%3.0f km/h", kmh);
    r.ui_text(8, H - 34, buf, rgba(255, 255, 255), 1.5f);
    std::snprintf(buf, sizeof buf, "COINS %d", k.coins);
    r.ui_text(8, H - 18, buf, rgba(255, 210, 60), 1.5f);

    // Mini-turbo charge bar, bottom centre
    if (k.drift == sim::DriftState::Drifting) {
        const float bw = 120.0f, bh = 6.0f, bx = W * 0.5f - bw * 0.5f, by = H - 22.0f;
        const float full = kc.mt_thresholds[2];
        r.ui_rect(bx - 1, by - 1, bw + 2, bh + 2, rgba(0, 0, 0, 160));
        const float fill = std::min(1.0f, k.mt_charge / full);
        r.ui_rect(bx, by, bw * fill, bh, tier_color(k.mt_tier(kc)));
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
        centered(r, W * 0.5f, H * 0.62f, label, rgba(255, 200, 80), 1.5f);
    }
    if (k.gliding) centered(r, W * 0.5f, H * 0.25f, "GLIDING  (stick up/down to pitch)", rgba(160, 210, 255), 1.0f);
    if (k.respawn_frames > 0) centered(r, W * 0.5f, H * 0.35f, "RESPAWNING...", rgba(255, 255, 255), 2.0f);

    // Countdown
    const int cd = w.countdown_left();
    if (cd > 0) {
        const int n = (cd + 59) / 60;
        std::snprintf(buf, sizeof buf, "%d", n);
        centered(r, W * 0.5f, H * 0.3f, buf, rgba(255, 255, 255), 8.0f);
    } else if (w.race_ticks() < 60) {
        centered(r, W * 0.5f, H * 0.3f, "GO!", rgba(80, 255, 120), 8.0f);
    }

    if (msg.time_left > 0.0f) centered(r, W * 0.5f, H * 0.42f, msg.text.c_str(), msg.color, 2.0f);

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
            "Arrows/WASD steer   X/Space gas   Z brake   Shift hop/drift/trick",
            "C look back   F2 low-res toggle   F1 hide help   R restart   Esc back",
            "Gamepad: stick steer, A gas, B brake, R drift, stick up/down pitch glider",
        };
        float y = H - 46.0f;
        for (const char* l : lines) {
            r.ui_text(W - render::Renderer::ui_text_width(l) - 6, y, l, rgba(255, 255, 255, 220), 1.0f);
            y += 10.0f;
        }
    }
}

} // namespace gk
