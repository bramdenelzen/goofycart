#pragma once
#include "input.h"
#include "render/renderer.h"
#include "settings.h"

#include "gk/sim/ai.h"
#include "gk/sim/stats.h"
#include "gk/sim/track_def.h"

#include <string>
#include <vector>

namespace gk {

enum class GameMode { GrandPrix, Versus, TimeTrial };

// The title menu: pick a mode.
struct TitleState {
    int row = 0;
};
enum class TitleChoice { None, GrandPrix, Versus, TimeTrial, Online, Options, Quit };

// ONLINE: host a race on this computer, join one by address, or quick match.
struct OnlineMenuState {
    int row = 0;
};
enum class OnlineChoice { None, Host, Join, QuickMatch, Back };
OnlineChoice update_online_menu(OnlineMenuState& s, const ClientActions& act);
void draw_online_menu(render::Renderer& r, const OnlineMenuState& s, bool quick_match_available);
// Typing a server address: the text so far, a hint underneath.
void draw_join(render::Renderer& r, const std::string& address, const std::string& hint, bool caret);
// Hosting: what friends type to join, while the server starts or runs.
void draw_hosting(render::Renderer& r, const std::vector<std::string>& lines, bool failed);
TitleChoice update_title(TitleState& s, const ClientActions& act);
void draw_title(render::Renderer& r, const TitleState& s);

// Cup or track choice plus race settings.
struct CourseState {
    GameMode mode = GameMode::GrandPrix;
    int cup = 0;
    int track = 0;      // index into the catalog (Versus, Time Trials)
    int row = 0;        // 0 cup/track, 1 engine class, 2 CPU level
    sim::EngineClass engine = sim::EngineClass::cc150;
    sim::CpuLevel cpu = sim::CpuLevel::Normal;
};
enum class CourseAction { None, Changed, Accept, Back };
CourseAction update_course(CourseState& s, const ClientActions& act, const sim::TrackCatalog& tracks);
void draw_course(render::Renderer& r, const CourseState& s, const sim::TrackCatalog& tracks);
// The track the course screen points at (the cup's first track in a Grand Prix).
std::string course_track(const CourseState& s, const sim::TrackCatalog& tracks);

// ONLINE while it signs in and looks for a race, or why it could not.
void draw_online_status(render::Renderer& r, const std::string& text, bool failed);

// OPTIONS > RECORD ENGINE: a count-in, anything said into the microphone (a
// held "ahhh", a word), then the result played back revving. Drawn over the options.
struct EngineRecording {
    enum class Phase { Off, CountIn, Recording, Result } phase = Phase::Off;
    float t = 0.0f;      // seconds into the phase
    float level = 0.0f;  // the microphone meter, 0..1
    bool ok = false;     // the take made an engine
    std::string message; // what the result says
};
inline constexpr float kCountInSeconds = 1.5f, kRecordSeconds = 2.5f, kResultSeconds = 3.5f;

// Volumes, display and sound settings.
struct OptionsState {
    int row = 0;
};
enum class OptionsAction { None, Changed, Controls, RecordEngine, Back };
OptionsAction update_options(OptionsState& s, Settings& settings, const ClientActions& act);
// `has_engine`: the player has recorded one. `rec`: a recording under way, drawn on top.
void draw_options(render::Renderer& r, const OptionsState& s, const Settings& settings, bool has_engine,
                  const EngineRecording& rec);

// CONTROLS: rebind the keyboard. `waiting` = the row whose key is being set.
struct ControlsState {
    int row = 0;
    int slot = 0;      // which of the two keys
    bool waiting = false;
};
enum class ControlsAction { None, StartCapture, Changed, Back };
ControlsAction update_controls(ControlsState& s, Settings& st, const ClientActions& act);
void draw_controls(render::Renderer& r, const ControlsState& s, const Settings& st);

const char* engine_name(sim::EngineClass e);
const char* cpu_name(sim::CpuLevel l);

} // namespace gk
