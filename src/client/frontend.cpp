#include "frontend.h"

#include <algorithm>
#include <cstdio>

namespace gk {
using render::rgba;

namespace {

void centered(render::Renderer& r, float y, const char* text, uint32_t color, float scale) {
    const float w = render::Renderer::ui_text_width(text, scale);
    r.ui_text((float(r.ui_extent().width) - w) * 0.5f, y, text, color, scale);
}

void footer(render::Renderer& r, const char* hint) {
    const float W = float(r.ui_extent().width), H = float(r.ui_extent().height);
    const float hw = render::Renderer::ui_text_width(hint, 1.5f);
    r.ui_rect((W - hw) * 0.5f - 6, H - 26, hw + 12, 18, rgba(0, 0, 0, 140));
    r.ui_text((W - hw) * 0.5f, H - 22, hint, rgba(255, 255, 255), 1.5f);
}

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
    const float H = float(r.ui_extent().height);
    float y = H * 0.16f;
    centered(r, y + 2, "GOBBY KART", rgba(0, 0, 0, 160), 5.0f);
    centered(r, y, "GOBBY KART", rgba(255, 220, 60), 5.0f);
    y += 70.0f;
    const float W = float(r.ui_extent().width);
    r.ui_rect(W * 0.5f - 90, y - 8, 180, 24.0f * kTitleCount + 10, rgba(0, 0, 0, 150));
    for (int i = 0; i < kTitleCount; ++i) {
        const bool sel = i == s.row;
        char buf[48];
        std::snprintf(buf, sizeof buf, sel ? "> %s <" : "%s", kTitleItems[i]);
        centered(r, y, buf, sel ? rgba(255, 230, 60) : rgba(255, 255, 255), 2.0f);
        y += 24.0f;
    }
    footer(r, "UP/DOWN: CHOOSE    ENTER / A: OK    ESC: QUIT");
}

// ---------------------------------------------------------------- online

void draw_online_status(render::Renderer& r, const std::string& text, bool failed) {
    const float W = float(r.ui_extent().width), H = float(r.ui_extent().height);
    const float w = std::max(200.0f, render::Renderer::ui_text_width(text.c_str(), 1.0f) + 24.0f);
    r.ui_rect((W - w) * 0.5f, H * 0.4f - 30, w, 64, rgba(0, 0, 0, 160));
    centered(r, H * 0.4f - 22, "ONLINE", rgba(255, 230, 60), 2.0f);
    centered(r, H * 0.4f + 10, text.c_str(), failed ? rgba(255, 120, 100) : rgba(255, 255, 255), 1.0f);
    footer(r, failed ? "ENTER / A: TRY AGAIN    ESC: BACK" : "ESC: BACK");
}

// ---------------------------------------------------------------- options

namespace {
constexpr const char* kOptionRows[] = {"VOLUME", "MUSIC", "EFFECTS", "LOW-RES LOOK", "FULLSCREEN", "CONTROL HINTS", "RUMBLE", "CRASH REPORTS", "BACK"};
constexpr int kOptionCount = 9;
} // namespace

OptionsAction update_options(OptionsState& s, Settings& st, const ClientActions& act) {
    if (act.back || (act.accept && s.row == kOptionCount - 1)) return OptionsAction::Back;
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
    const float x = 12.0f;
    float y = 12.0f;
    r.ui_rect(x - 6, y - 6, 260, 46.0f + 16.0f * kOptionCount, rgba(0, 0, 0, 150));
    r.ui_text(x, y, "OPTIONS", rgba(255, 230, 60), 2.0f);
    y += 26.0f;
    for (int i = 0; i < kOptionCount; ++i) {
        const bool sel = i == s.row;
        if (sel) r.ui_rect(x - 3, y - 3, 254, 15, rgba(255, 200, 40, 80));
        r.ui_text(x, y, kOptionRows[i], sel ? rgba(255, 230, 60) : rgba(200, 200, 200), 1.0f);
        const float vx = x + 120;
        if (i <= 2) {
            const int v = i == 0 ? st.master : i == 1 ? st.music : st.sfx;
            for (int b = 0; b < 10; ++b)
                r.ui_rect(vx + float(b) * 11, y, 9, 8, b < v ? rgba(90, 200, 255) : rgba(255, 255, 255, 40));
        } else if (i < kOptionCount - 1) {
            const bool on = i == 3 ? st.low_res : i == 4 ? st.fullscreen : i == 5 ? st.show_help : i == 6 ? st.rumble : st.crash_reports;
            r.ui_text(vx, y, on ? "< ON >" : "< OFF >", rgba(255, 255, 255), 1.0f);
        }
        y += 16.0f;
    }
    footer(r, "UP/DOWN: CHOOSE    LEFT/RIGHT: CHANGE    ESC: BACK");
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
    const float x = 12.0f;
    float y = 12.0f;
    const bool gp = s.mode == GameMode::GrandPrix;
    const int rows = s.mode == GameMode::TimeTrial ? 2 : 3;
    const float panel_h = 46.0f + 16.0f * float(rows) + (gp ? 56.0f : 0.0f);
    r.ui_rect(x - 6, y - 6, 260, panel_h, rgba(0, 0, 0, 150));
    r.ui_text(x, y, gp ? "GRAND PRIX" : s.mode == GameMode::Versus ? "VS RACE" : "TIME TRIALS", rgba(255, 230, 60), 2.0f);
    y += 26.0f;
    auto row = [&](int i, const char* label, const std::string& value) {
        const bool sel = i == s.row;
        if (sel) r.ui_rect(x - 3, y - 3, 254, 15, rgba(255, 200, 40, 80));
        r.ui_text(x, y, label, sel ? rgba(255, 230, 60) : rgba(200, 200, 200), 1.0f);
        std::snprintf(buf, sizeof buf, "< %s >", value.c_str());
        r.ui_text(x + 60, y, buf, rgba(255, 255, 255), 1.0f);
        y += 16.0f;
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
        y += 4.0f;
        const auto& cup = tracks.cups[size_t(s.cup % int(tracks.cups.size()))];
        for (size_t i = 0; i < cup.tracks.size(); ++i) {
            const int ti = tracks.find(cup.tracks[i]);
            std::snprintf(buf, sizeof buf, "%zu. %s", i + 1, ti >= 0 ? tracks.tracks[size_t(ti)].name.c_str() : "?");
            r.ui_text(x + 8, y, buf, rgba(220, 230, 255), 1.0f);
            y += 12.0f;
        }
    }
    footer(r, "ENTER / A: CHOOSE KART    ESC: BACK");
}

} // namespace gk
