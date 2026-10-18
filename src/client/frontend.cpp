#include "frontend.h"

#include "ui_style.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace gk {
using render::rgba;

namespace {

void centered(render::Renderer& r, float y, const char* text, uint32_t color, float scale) {
    ui::text_c(r, ui::W(r) * 0.5f, y, text, color, scale);
}

void footer(render::Renderer& r, const char* hint) { ui::footer(r, hint); }

int step(const ClientActions& act) { return (act.menu_right ? 1 : 0) - (act.menu_left ? 1 : 0); }

} // namespace

const char* engine_name(sim::EngineClass e) {
    switch (e) {
    case sim::EngineClass::cc50: return "50cc";
    case sim::EngineClass::cc100: return "100cc";
    case sim::EngineClass::cc150: return "150cc";
    case sim::EngineClass::cc200: return "200cc";
    }
    return "?";
}

const char* cpu_name(sim::CpuLevel l) {
    switch (l) {
    case sim::CpuLevel::Easy: return "EASY";
    case sim::CpuLevel::Normal: return "NORMAL";
    case sim::CpuLevel::Hard: return "HARD";
    }
    return "?";
}

// ---------------------------------------------------------------- title

namespace {
constexpr const char* kTitleItems[] = {"GRAND PRIX", "VS RACE", "TIME TRIALS", "ONLINE", "OPTIONS", "QUIT"};
constexpr int kTitleCount = 6;
} // namespace

TitleChoice update_title(TitleState& s, const ClientActions& act) {
    if (act.menu_up) s.row = (s.row + kTitleCount - 1) % kTitleCount;
    if (act.menu_down) s.row = (s.row + 1) % kTitleCount;
    if (act.back) return TitleChoice::Quit;
    if (!act.accept) return TitleChoice::None;
    switch (s.row) {
    case 0: return TitleChoice::GrandPrix;
    case 1: return TitleChoice::Versus;
    case 2: return TitleChoice::TimeTrial;
    case 3: return TitleChoice::Online;
    case 4: return TitleChoice::Options;
    default: return TitleChoice::Quit;
    }
}

void draw_title(render::Renderer& r, const TitleState& s) {
    const float W = ui::W(r), H = ui::H(r);
    const float y = H * 0.1f;
    // The logo: two words, a little tilted, on a gold banner.
    const float s1 = 5.2f;
    const float w1 = ui::text_w("GOBBY", s1), w2 = ui::text_w("KART", s1), gap = 10.0f;
    const float x0 = W * 0.5f - (w1 + gap + w2) * 0.5f;
    // Speed stripes behind the logo, then the words in chrome gold and red.
    for (int i = 0; i < 3; ++i)
        r.ui_slant_rect(x0 - 40.0f + 14.0f * float(i), y + 7.0f * s1 + 6.0f + 5.0f * float(i), w1 + gap + w2 + 60.0f - 28.0f * float(i), 3.0f,
                        ui::kBarSlant, i == 0 ? ui::kRed : i == 1 ? ui::kGold : rgba(255, 255, 255, 200), i == 0 ? ui::kRed : i == 1 ? ui::kGold : rgba(255, 255, 255, 200));
    ui::heading(r, x0, y, "GOBBY", s1);
    ui::heading(r, x0 + w1 + gap, y, "KART", s1, rgba(255, 120, 100), rgba(200, 0, 10));
    ui::text_c(r, W * 0.5f, y + 7.0f * s1 + 26.0f, "RACE   DRIFT   SMASH", rgba(255, 255, 255, 230), 1.2f);
    // The menu on the left third, so the kart on the right stays in view.
    const float bw = 180.0f, top = y + 7.0f * s1 + 62.0f, cx = std::max(W * 0.3f, bw * 0.5f + 30.0f);
    ui::panel(r, cx - bw * 0.5f - 14.0f, top - 12.0f, bw + 28.0f, 27.0f * kTitleCount + 18.0f, 14.0f);
    ui::button_list(r, cx, top, bw, kTitleItems, kTitleCount, s.row);
    footer(r, "UP/DOWN  CHOOSE      ENTER / A  OK      ESC  QUIT");
}

// ---------------------------------------------------------------- online

namespace {
constexpr const char* kOnlineItems[] = {"HOST A GAME", "JOIN A GAME", "QUICK MATCH", "BACK"};
constexpr int kOnlineCount = 4;
} // namespace

OnlineChoice update_online_menu(OnlineMenuState& s, const ClientActions& act) {
    if (act.menu_up) s.row = (s.row + kOnlineCount - 1) % kOnlineCount;
    if (act.menu_down) s.row = (s.row + 1) % kOnlineCount;
    if (act.back) return OnlineChoice::Back;
    if (!act.accept) return OnlineChoice::None;
    switch (s.row) {
    case 0: return OnlineChoice::Host;
    case 1: return OnlineChoice::Join;
    case 2: return OnlineChoice::QuickMatch;
    default: return OnlineChoice::Back;
    }
}

void draw_online_menu(render::Renderer& r, const OnlineMenuState& s, bool quick_match_available) {
    const float W = ui::W(r), H = ui::H(r);
    float y = H * 0.14f;
    ui::title(r, W * 0.5f, y, "ONLINE", 3.4f);
    y += 44.0f;
    const float bw = 210.0f;
    ui::panel(r, W * 0.5f - bw * 0.5f - 14.0f, y - 12.0f, bw + 28.0f, 27.0f * kOnlineCount + 44.0f, 14.0f);
    y = ui::button_list(r, W * 0.5f, y, bw, kOnlineItems, kOnlineCount, s.row, quick_match_available ? 0u : 4u);
    const char* help = s.row == 0   ? "Run a race server on this computer and play on it"
                       : s.row == 1 ? "Race on a friend's server: type or paste its address"
                       : s.row == 2 ? (quick_match_available ? "Find a race through the matchmaking server" : "Needs a matchmaking server (--master URL)")
                                    : "";
    ui::text_c(r, W * 0.5f, y + 2.0f, help, ui::kSubtle, 1.1f);
    footer(r, "UP/DOWN  CHOOSE      ENTER / A  OK      ESC  BACK");
}

void draw_join(render::Renderer& r, const std::string& address, const std::string& hint, bool caret) {
    const float W = ui::W(r), H = ui::H(r);
    const float pw = 320.0f, x = (W - pw) * 0.5f, y = H * 0.24f;
    ui::panel(r, x, y, pw, 128.0f, 14.0f);
    ui::title(r, W * 0.5f, y + 12.0f, "JOIN A GAME", 2.4f);
    ui::text_c(r, W * 0.5f, y + 42.0f, "Server address (the host's screen shows it)", ui::kSubtle, 1.1f);
    r.ui_slant_rect(x + 22.0f, y + 58.0f, pw - 44.0f, 28.0f, ui::kSlant, rgba(0, 0, 0, 230), rgba(20, 20, 26, 230));
    r.ui_slant_rect(x + 22.0f, y + 84.0f, pw - 44.0f, 2.0f, 0.0f, ui::kGold, ui::kGold);
    const std::string shown = address + (caret ? "|" : " ");
    ui::text_c(r, W * 0.5f, y + 65.0f, shown.c_str(), ui::kText, 2.0f);
    ui::text_c(r, W * 0.5f, y + 100.0f, hint.c_str(), ui::kWarn, 1.1f);
    footer(r, "TYPE OR CTRL/CMD+V TO PASTE      ENTER  JOIN      ESC  BACK");
}

void draw_hosting(render::Renderer& r, const std::vector<std::string>& lines, bool failed) {
    const float W = ui::W(r), H = ui::H(r);
    float w = 260.0f;
    for (const auto& l : lines) w = std::max(w, ui::text_w(l.c_str(), 1.2f) + 40.0f);
    const float y = H * 0.24f;
    ui::panel(r, (W - w) * 0.5f, y, w, 52.0f + 15.0f * float(lines.size()), 14.0f);
    ui::title(r, W * 0.5f, y + 12.0f, "HOST A GAME", 2.4f);
    float ly = y + 42.0f;
    for (const auto& l : lines) {
        ui::text_c(r, W * 0.5f, ly, l.c_str(), failed ? ui::kWarn : ui::kText, 1.2f);
        ly += 15.0f;
    }
    footer(r, failed ? "ESC  BACK" : "ESC  CANCEL");
}

void draw_online_status(render::Renderer& r, const std::string& text, bool failed) {
    const float W = ui::W(r), H = ui::H(r);
    const float w = std::max(240.0f, ui::text_w(text.c_str(), 1.2f) + 40.0f);
    const float y = H * 0.32f;
    ui::panel(r, (W - w) * 0.5f, y, w, 70.0f, 14.0f);
    ui::title(r, W * 0.5f, y + 12.0f, "ONLINE", 2.4f);
    ui::text_c(r, W * 0.5f, y + 46.0f, text.c_str(), failed ? ui::kWarn : ui::kText, 1.2f);
    footer(r, failed ? "ENTER / A  TRY AGAIN      ESC  BACK" : "ESC  BACK");
}

// ---------------------------------------------------------------- options

namespace {
constexpr const char* kOptionRows[] = {"VOLUME", "MUSIC", "EFFECTS", "LOW-RES LOOK", "FULLSCREEN", "CONTROL HINTS", "RUMBLE", "CRASH REPORTS", "CONTROLS", "BACK"};
constexpr int kOptionCount = 10;
} // namespace

OptionsAction update_options(OptionsState& s, Settings& st, const ClientActions& act) {
    if (act.back || (act.accept && s.row == kOptionCount - 1)) return OptionsAction::Back;
    if (act.accept && s.row == kOptionCount - 2) return OptionsAction::Controls;
    if (act.menu_up) s.row = (s.row + kOptionCount - 1) % kOptionCount;
    if (act.menu_down) s.row = (s.row + 1) % kOptionCount;
    const int d = step(act) + (act.accept ? 1 : 0);
    if (d == 0) return OptionsAction::None;
    auto vol = [d](int& v) { v = std::clamp(v + (d > 0 ? 1 : -1), 0, 10); };
    switch (s.row) {
    case 0: vol(st.master); break;
    case 1: vol(st.music); break;
    case 2: vol(st.sfx); break;
    case 3: st.low_res = !st.low_res; break;
    case 4: st.fullscreen = !st.fullscreen; break;
    case 5: st.show_help = !st.show_help; break;
    case 6: st.rumble = !st.rumble; break;
    case 7: st.crash_reports = !st.crash_reports; break;
    default: return OptionsAction::None;
    }
    return OptionsAction::Changed;
}

void draw_options(render::Renderer& r, const OptionsState& s, const Settings& st) {
    const float W = ui::W(r);
    const float pw = 300.0f, x = (W - pw) * 0.5f, y = 18.0f, rh = 19.0f;
    ui::panel(r, x, y, pw, 54.0f + rh * kOptionCount, 14.0f);
    ui::title(r, W * 0.5f, y + 12.0f, "OPTIONS", 2.6f);
    float ry = y + 44.0f;
    for (int i = 0; i < kOptionCount; ++i) {
        const bool sel = i == s.row;
        const float rx = x + 14.0f, rw = pw - 28.0f;
        if (i <= 2) {
            ui::option_row(r, rx, ry, rw, kOptionRows[i], nullptr, sel);
            const int v = i == 0 ? st.master : i == 1 ? st.music : st.sfx;
            for (int b = 0; b < 10; ++b) {
                const float bx = rx + rw - 12.0f - float(10 - b) * 11.0f;
                r.ui_slant_rect(bx, ry + 4.0f, 7.0f, 8.0f, ui::kBarSlant, b < v ? ui::kGold : rgba(255, 255, 255, sel ? 90 : 40),
                                b < v ? ui::kGoldDeep : rgba(255, 255, 255, sel ? 90 : 40));
            }
        } else if (i < kOptionCount - 2) {
            const bool on = i == 3 ? st.low_res : i == 4 ? st.fullscreen : i == 5 ? st.show_help : i == 6 ? st.rumble : st.crash_reports;
            ui::option_row(r, rx, ry, rw, kOptionRows[i], on ? "<  ON  >" : "<  OFF  >", sel);
        } else {
            ui::option_row(r, rx, ry, rw, kOptionRows[i], nullptr, sel);
        }
        ry += rh;
    }
    footer(r, "UP/DOWN  CHOOSE      LEFT/RIGHT  CHANGE      ESC  BACK");
}

// ---------------------------------------------------------------- controls

ControlsAction update_controls(ControlsState& s, Settings& st, const ClientActions& act) {
    const int rows = kBindCount + 2; // the binds, RESET, BACK
    if (act.back) return ControlsAction::Back;
    if (act.menu_up) s.row = (s.row + rows - 1) % rows;
    if (act.menu_down) s.row = (s.row + 1) % rows;
    if (act.menu_left || act.menu_right) s.slot = 1 - s.slot;
    if (!act.accept) return ControlsAction::None;
    if (s.row == rows - 1) return ControlsAction::Back;
    if (s.row == rows - 2) {
        st.binds = default_binds();
        return ControlsAction::Changed;
    }
    s.waiting = true;
    return ControlsAction::StartCapture;
}

void draw_controls(render::Renderer& r, const ControlsState& s, const Settings& st) {
    const float W = ui::W(r);
    const float pw = 400.0f, x = (W - pw) * 0.5f, y = 14.0f, rh = 18.0f;
    const int rows = kBindCount + 2;
    ui::panel(r, x, y, pw, 54.0f + rh * float(rows), 14.0f);
    ui::title(r, W * 0.5f, y + 10.0f, "CONTROLS", 2.4f);
    float ry = y + 40.0f;
    auto key_name = [](int sc) -> std::string {
        if (sc <= 0) return "-";
        const char* n = SDL_GetScancodeName(SDL_Scancode(sc));
        std::string out = n && *n ? n : "?";
        for (char& ch : out) ch = char(std::toupper(static_cast<unsigned char>(ch)));
        return out;
    };
    for (int i = 0; i < rows; ++i) {
        const bool sel = i == s.row;
        const float rx = x + 14.0f, rw = pw - 28.0f;
        if (i < kBindCount) {
            ui::option_row(r, rx, ry, rw, bind_name(Bind(i)), nullptr, sel);
            for (int k = 0; k < 2; ++k) {
                const bool active = sel && k == s.slot;
                const std::string txt = active && s.waiting ? "PRESS A KEY" : key_name(st.binds[size_t(i)][size_t(k)]);
                const float kx = rx + rw - 200.0f + float(k) * 100.0f;
                if (active) r.ui_slant_rect(kx - 4.0f, ry + 1.0f, 96.0f, rh - 4.0f, ui::kBarSlant, ui::kGold, ui::kGoldDeep);
                const float tw = ui::text_w(txt.c_str(), 1.0f);
                if (active) r.ui_text_ex(kx + 44.0f - tw * 0.5f, ry + 4.5f, txt.c_str(), ui::kInk, 1.0f, 0, 0.0f, false, 0.9f);
                else ui::text_c(r, kx + 44.0f, ry + 4.5f, txt.c_str(), ui::kText, 1.0f);
            }
        } else {
            ui::option_row(r, rx, ry, rw, i == kBindCount ? "RESET TO DEFAULTS" : "BACK", nullptr, sel);
        }
        ry += rh;
    }
    footer(r, s.waiting ? "PRESS THE NEW KEY      ESC  CANCEL" : "UP/DOWN  CHOOSE      LEFT/RIGHT  FIRST / SECOND KEY      ENTER  REBIND      ESC  BACK");
}

// ---------------------------------------------------------------- course

CourseAction update_course(CourseState& s, const ClientActions& act, const sim::TrackCatalog& tracks) {
    if (act.back) return CourseAction::Back;
    if (act.accept) return CourseAction::Accept;
    const int rows = s.mode == GameMode::TimeTrial ? 2 : 3;
    if (act.menu_up) s.row = (s.row + rows - 1) % rows;
    if (act.menu_down) s.row = (s.row + 1) % rows;
    const int d = step(act);
    if (d == 0) return CourseAction::None;
    switch (s.row) {
    case 0:
        if (s.mode == GameMode::GrandPrix) {
            const int n = std::max(1, int(tracks.cups.size()));
            s.cup = (s.cup + d + n) % n;
        } else {
            const int n = std::max(1, int(tracks.tracks.size()));
            s.track = (s.track + d + n) % n;
        }
        break;
    case 1: s.engine = sim::EngineClass((int(s.engine) + d + 4) % 4); break;
    default: s.cpu = sim::CpuLevel((int(s.cpu) + d + 3) % 3); break;
    }
    return CourseAction::Changed;
}

std::string course_track(const CourseState& s, const sim::TrackCatalog& tracks) {
    if (s.mode == GameMode::GrandPrix) {
        if (tracks.cups.empty()) return {};
        const auto& cup = tracks.cups[size_t(std::clamp(s.cup, 0, int(tracks.cups.size()) - 1))];
        return cup.tracks.empty() ? std::string{} : cup.tracks.front();
    }
    if (tracks.tracks.empty()) return {};
    return tracks.tracks[size_t(std::clamp(s.track, 0, int(tracks.tracks.size()) - 1))].id;
}

void draw_course(render::Renderer& r, const CourseState& s, const sim::TrackCatalog& tracks) {
    char buf[96];
    const bool gp = s.mode == GameMode::GrandPrix;
    const int rows = s.mode == GameMode::TimeTrial ? 2 : 3;
    const float x = 16.0f, pw = 270.0f, rh = 19.0f;
    float y = 16.0f;
    const int ntracks = gp && !tracks.cups.empty() ? int(tracks.cups[size_t(s.cup % int(tracks.cups.size()))].tracks.size()) : 0;
    ui::panel(r, x, y, pw, 52.0f + rh * float(rows) + (gp ? 18.0f + 14.0f * float(ntracks) : 0.0f), 14.0f);
    const char* heading = gp ? "GRAND PRIX" : s.mode == GameMode::Versus ? "VS RACE" : "TIME TRIALS";
    ui::heading(r, x + 14.0f, y + 12.0f, heading, 2.6f);
    y += 44.0f;
    auto row = [&](int i, const char* label, const std::string& value) {
        std::snprintf(buf, sizeof buf, "<  %s  >", value.c_str());
        ui::option_row(r, x + 12.0f, y, pw - 24.0f, label, buf, i == s.row);
        y += rh;
    };
    if (gp) {
        const std::string cup = tracks.cups.empty() ? "-" : tracks.cups[size_t(s.cup % int(tracks.cups.size()))].name;
        row(0, "CUP", cup);
    } else {
        const std::string name = tracks.tracks.empty() ? "-" : tracks.tracks[size_t(s.track % int(tracks.tracks.size()))].name;
        row(0, "TRACK", name);
    }
    row(1, "CLASS", engine_name(s.engine));
    if (rows > 2) row(2, "CPU", cpu_name(s.cpu));
    if (gp && !tracks.cups.empty()) {
        y += 8.0f;
        const auto& cup = tracks.cups[size_t(s.cup % int(tracks.cups.size()))];
        for (size_t i = 0; i < cup.tracks.size(); ++i) {
            const int ti = tracks.find(cup.tracks[i]);
            std::snprintf(buf, sizeof buf, "%zu", i + 1);
            ui::badge(r, x + 18.0f, y - 1.0f, 16.0f, 12.0f, ui::kRed, buf, ui::kText, 1.0f);
            r.ui_text(x + 40.0f, y + 1.0f, ti >= 0 ? tracks.tracks[size_t(ti)].name.c_str() : "?", ui::kText, 1.1f);
            y += 14.0f;
        }
    }
    footer(r, "ENTER / A  CHOOSE KART      ESC  BACK");
}

} // namespace gk
