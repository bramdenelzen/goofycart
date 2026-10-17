#pragma once
#include "input.h"
#include "render/renderer.h"

#include "gk/sim/ai.h"
#include "gk/sim/stats.h"
#include "gk/sim/track_def.h"

#include <string>

namespace gk {

enum class GameMode { GrandPrix, Versus, TimeTrial };

// The title menu: pick a mode.
struct TitleState {
    int row = 0;
};
enum class TitleChoice { None, GrandPrix, Versus, TimeTrial, Quit };
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

const char* engine_name(sim::EngineClass e);
const char* cpu_name(sim::CpuLevel l);

} // namespace gk
