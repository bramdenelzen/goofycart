#include "app.h"
#include "ui_style.h"

#include <filesystem>

#include "render/item_mesh.h"
#include "render/kart_mesh.h"

#include "gk/online/master_client.h"
#include "render/track_mesh.h"
#include "render/trackside.h"

#include "gk/core/crash.h"
#include "gk/core/fixed_step.h"
#include "gk/core/log.h"
#include "gk/sim/data.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <fstream>
#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace gk {
namespace {

uint32_t tier_color(int tier) {
    switch (tier) {
    case 1: return render::rgba(80, 160, 255);
    case 2: return render::rgba(255, 140, 30);
    case 3: return render::rgba(200, 80, 255);
    default: return render::rgba(255, 240, 160);
    }
}

vec4 to_tint(uint32_t c) {
    return vec4(float(c & 255) / 255.0f, float((c >> 8) & 255) / 255.0f, float((c >> 16) & 255) / 255.0f, 1.0f);
}

} // namespace

int App::run(const AppOptions& options) {
    options_ = options;
    if (!init()) {
        shutdown();
        return 1;
    }
    FixedStep step;
    uint64_t last = SDL_GetPerformanceCounter();
    const double freq = double(SDL_GetPerformanceFrequency());
    bool running = true;
    while (running) {
        input_.begin_frame();
        SDL_Event e;
        while (SDL_PollEvent(&e)) input_.handle_event(e);
        const ClientActions& act = input_.actions();
        if (act.quit_requested) running = false;
        if (act.toggle_resolution_pressed) {
            settings_.low_res = !settings_.low_res;
            apply_settings();
        }
        if (act.toggle_help_pressed) {
            settings_.show_help = !settings_.show_help;
            apply_settings();
        }
        if (net_) net_->service([this](std::span<const uint8_t> d) { online_->on_message(d); });
        if (screen_ != Screen::Race) {
            if (act.accept) audio_.mixer().play(audio::Sfx::MenuAccept);
            else if (act.back) audio_.mixer().play(audio::Sfx::MenuBack);
            else if (act.menu_up || act.menu_down || act.menu_left || act.menu_right) audio_.mixer().play(audio::Sfx::MenuMove);
        }
        poll_matchmaking();
        poll_hosting();
        handle_menu_actions(act, running);

        const uint64_t now = SDL_GetPerformanceCounter();
        const double elapsed = double(now - last) / freq;
        last = now;
        // Scripted runs step exactly one tick per frame so output is reproducible.
        const bool scripted = options_.max_frames >= 0;
        // Online, the client clock runs slightly fast or slow to keep its
        // inputs reaching the server just before they are needed.
        const double scale = online_ ? online_->time_scale() : 1.0;
        const int ticks = scripted ? 1 : step.advance(elapsed * scale);
        for (int i = 0; i < ticks; ++i) update();
        update_audio();

        if (scripted && int(frame_) == options_.max_frames - 1 && !options_.screenshot_path.empty())
            renderer_.request_screenshot(options_.screenshot_path);
        render(scripted ? 1.0f : step.alpha(), scripted ? kDt : float(elapsed));
        ++frame_;
        if (scripted && int(frame_) >= options_.max_frames) running = false;
    }
    shutdown();
    return 0;
}

bool App::init() {
    std::string err;
    if (!tables_.load(sim::data_path("physics.json"), &err) || !parts_.load(sim::data_path("parts.json"), &err) ||
        !tracks_.load(sim::data_path("tracks/tracks.json"), &err)) {
        log_error("Could not load game data: {}", err);
        return false;
    }
    course_.engine = options_.engine;
    course_.cpu = options_.cpu_level;
#ifdef __APPLE__
    // Which Vulkan library to use: SDL_VULKAN_LIBRARY if set, else the MoltenVK
    // a packaged app carries in Contents/Frameworks, else Homebrew's loader or
    // MoltenVK (dlopen does not search /opt/homebrew/lib by itself).
    if (!SDL_GetHint(SDL_HINT_VULKAN_LIBRARY)) {
        std::vector<std::string> candidates;
        if (const char* base = SDL_GetBasePath()) candidates.push_back(std::string(base) + "../Frameworks/libMoltenVK.dylib");
        for (const char* prefix : {"/opt/homebrew/lib/", "/usr/local/lib/"})
            for (const char* lib : {"libvulkan.1.dylib", "libMoltenVK.dylib"}) candidates.push_back(std::string(prefix) + lib);
        for (const std::string& lib : candidates)
            if (std::ifstream(lib).good()) {
                SDL_SetHint(SDL_HINT_VULKAN_LIBRARY, lib.c_str());
                log_info("Vulkan library: {}", lib);
                break;
            }
    }
#endif
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        log_error("SDL_Init failed: {}", SDL_GetError());
        return false;
    }
    if (char* pref = SDL_GetPrefPath("Gobby", "GobbyKart")) {
        pref_dir_ = pref;
        SDL_free(pref);
        install_crash_handler(pref_dir_ + "crashes", "gk_client", GK_VERSION);
    }
    window_ = SDL_CreateWindow("Gobby Kart", options_.window_width, options_.window_height,
                               SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window_) {
        log_error("SDL_CreateWindow failed: {}", SDL_GetError());
        return false;
    }
    render::RendererOptions ro;
    ro.full_resolution = options_.full_resolution;
    ro.validation = options_.validation;
    if (!renderer_.init(window_, ro)) return false;
    if (!renderer_.load_font(sim::data_path("fonts/Nunito.ttf"))) log_warn("UI font missing: using the pixel font");
    // The game's own painted items, then an optional model pack beside the
    // data folder (not in the repository) that may override them.
    // Low-poly: half the segments of the smooth shapes; the painted textures
    // carry the detail.
    render::MeshBuilder::detail = 0.5f;
    for (const auto& [name, model] : render::make_gobby_items()) assets_.add_builtin_item(name, model, renderer_);
    for (const auto& tire : parts_.tires) assets_.add_builtin_tire(tire.id, render::make_gobby_tire(tire.look), renderer_);
    // Map props: kept on the CPU to merge per track, with one texture each.
    for (auto& [name, model] : render::make_gobby_props()) {
        const render::TextureId tex = renderer_.upload_texture(model.texture.size(), model.texture.size(), model.texture.rgba().data());
        props_.emplace(name, PropModel{std::move(model.mesh), tex, model.two_sided, model.blended, model.bounds_min, model.bounds_max, model.glow, model.unlit});
    }
    if (assets_.load(sim::data_path("../assets/models"), renderer_)) log_info("Model pack loaded from assets/models");
    if (!options_.export_assets.empty()) {
        int written = 0;
        std::string err;
        for (const auto& [name, model] : render::make_gobby_items())
            if (render::export_painted_model(model, options_.export_assets + "/items", name, &err)) ++written;
            else log_warn("{}", err);
        for (const auto& [name, model] : render::make_gobby_props())
            if (render::export_painted_model(model, options_.export_assets + "/props", name, &err)) ++written;
            else log_warn("{}", err);
        if (render::export_painted_model(render::make_gobby_kart(render::rgba(225, 45, 40)), options_.export_assets + "/karts", "standard_kart", &err)) ++written;
        for (const auto& tire : parts_.tires)
            if (render::export_painted_model(render::make_gobby_tire(tire.look), options_.export_assets + "/tires", tire.id, &err)) ++written;
        log_info("Exported {} models to {}", written, options_.export_assets);
    }
    if (const std::string sp = settings_path(); !sp.empty()) settings_.load(sp);
    if (options_.full_resolution) settings_.low_res = false;
    apply_settings();
    if (const std::string url = !options_.master_url.empty() ? options_.master_url : settings_.master_url;
        settings_.crash_reports && !url.empty() && !pref_dir_.empty())
        crash_upload_ = std::async(std::launch::async, [url, dir = pref_dir_ + "crashes"] {
            online::MasterClient(url).send_crash_reports(dir);
        });
    // Scripted runs (CI, screenshots) mix into SDL's dummy device so the audio
    // path still runs without speakers.
    if (options_.max_frames >= 0) SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    if (!options_.mute) audio_.open();

    select_.build = options_.build;
    if (!options_.connect_host.empty() && !connect_online(options_.connect_host, options_.connect_port, {})) return false;
    if (options_.host) start_hosting();
    // --track (and demos) race straight away, skipping the menus.
    if (!online_ && (options_.autodrive || !options_.track.empty())) {
        course_.mode = options_.time_trial ? GameMode::TimeTrial : GameMode::Versus;
        if (options_.track.empty()) options_.track = tracks_.tracks.empty() ? "greybox" : tracks_.tracks.front().id;
        if (const int i = tracks_.find(options_.track); i >= 0) course_.track = i;
        start_race();
        screen_ = Screen::Race;
        paused_ = options_.menu == "pause"; // screenshots of the pause menu
        if (world_ && options_.spawn_obstacle >= 0 && size_t(options_.spawn_obstacle) < world_->track().obstacles.size()) {
            const auto& o = world_->track().obstacles[size_t(options_.spawn_obstacle)];
            sim::KartState& k = world_->karts_mut()[size_t(player_)];
            k.pos = o.pos + o.up * 0.3f - o.forward * 14.0f;
            k.rot = glm::normalize(glm::quat_cast(glm::mat3(-o.right, o.up, o.forward)));
            k.sample = world_->track().nearest_sample(k.pos);
            prev_ = world_->karts();
        }
    } else {
        if (options_.menu == "options") screen_ = Screen::Options;
        else if (options_.menu == "course") screen_ = Screen::Course;
        else if (options_.menu == "online") start_matchmaking();
        show_track(course_track(course_, tracks_));
    }
    {   // A soft round shadow under each kart.
        render::Canvas c(128, vec4(0.0f));
        c.glow({0.0f, 0.0f, 1.0f, 1.0f}, {0.5f, 0.5f}, 0.5f, vec4(0.0f, 0.0f, 0.03f, 0.35f), 0.6f);
        render::MeshBuilder q;
        q.quad_uv({-1, 0, -1}, {-1, 0, 1}, {1, 0, 1}, {1, 0, -1}, {0, 0}, {0, 1}, {1, 1}, {1, 0}, render::rgba(255, 255, 255));
        shadow_mesh_ = renderer_.upload_mesh(q.take(), renderer_.upload_texture(128, 128, c.rgba().data()), false, true, true);
    }
    spark_mesh_ = renderer_.upload_mesh(render::make_spark_mesh());
    flame_mesh_ = renderer_.upload_mesh(render::make_flame_mesh());
    box_mesh_ = renderer_.upload_mesh(render::make_item_box_mesh());
    banana_mesh_ = renderer_.upload_mesh(render::make_banana_mesh());
    green_mesh_ = renderer_.upload_mesh(render::make_shell_mesh(render::rgba(60, 200, 70), false));
    red_mesh_ = renderer_.upload_mesh(render::make_shell_mesh(render::rgba(230, 50, 40), false));
    spiked_mesh_ = renderer_.upload_mesh(render::make_shell_mesh(render::rgba(60, 110, 240), true));
    bomb_mesh_ = renderer_.upload_mesh(render::make_bomb_mesh());
    ball_mesh_ = renderer_.upload_mesh(render::make_ball_mesh(render::rgba(255, 255, 255)));
    boomerang_mesh_ = renderer_.upload_mesh(render::make_boomerang_mesh());
    bullet_mesh_ = renderer_.upload_mesh(render::make_bullet_mesh());
    chomper_mesh_ = renderer_.upload_mesh(render::make_chomper_mesh());
    set_kart_meshes({});
    if (!options_.crash.empty()) crash_now(options_.crash);
    log_info("Renderer ready, internal resolution {}x{}", renderer_.internal_extent().width,
             renderer_.internal_extent().height);
    return true;
}

const App::KartVisual& App::build_visual(const sim::Build& b, int slot) {
    const uint64_t key = uint64_t(slot % 12) | uint64_t(b.driver & 0xff) << 8 | uint64_t(b.body & 0xff) << 16 |
                         uint64_t(b.tires & 0xff) << 24 | uint64_t(b.glider & 0xff) << 32;
    auto it = build_visuals_.find(key);
    if (it != build_visuals_.end()) return it->second;
    KartVisual v;
    const render::KartLook look = render::kart_look(parts_, b, kart_color(slot));
    auto id_of = [](const std::vector<sim::Part>& parts, int i) {
        return parts.empty() ? std::string() : parts[size_t(std::clamp(i, 0, int(parts.size()) - 1))].id;
    };
    v.driver_model = assets_.driver(id_of(parts_.drivers, b.driver));
    v.body_model = assets_.body(id_of(parts_.bodies, b.body));
    // The painted standard kart, one per racer colour, unless a pack replaces it.
    bool gobby_kart = false;
    if (!v.body_model && id_of(parts_.bodies, b.body) == "standard") {
        auto it = gobby_karts_.find(look.paint);
        if (it == gobby_karts_.end())
            it = gobby_karts_.emplace(look.paint, AssetLibrary::upload_painted(render::make_gobby_kart(look.paint), renderer_)).first;
        v.body_model = &it->second;
        gobby_kart = true;
    }
    v.tire_model = assets_.tire(id_of(parts_.tires, b.tires));
    v.glider_model = assets_.glider(id_of(parts_.gliders, b.glider));
    // Procedural pieces for whatever the pack doesn't cover, and the layout.
    render::MeshData full = render::make_kart_mesh(look, &v.layout, render::KartAll);
    if (!v.body_model && !v.driver_model && !v.tire_model) {
        v.body = renderer_.upload_mesh(full);
    } else {
        if (!v.body_model) v.body = renderer_.upload_mesh(render::make_kart_mesh(look, nullptr, render::KartBody));
        if (!v.driver_model) v.driver = renderer_.upload_mesh(render::make_kart_mesh(look, nullptr, render::KartDriver));
        if (!v.tire_model) v.wheels = renderer_.upload_mesh(render::make_kart_mesh(look, nullptr, render::KartWheels));
    }
    // A body model may say where its wheels and seat are.
    if (v.body_model)
        if (const ModelSpec* spec = assets_.body_spec(id_of(parts_.bodies, b.body))) {
            if (!spec->wheels.empty()) {
                const float r = spec->wheel_radius > 0.0f ? spec->wheel_radius : v.layout.wheels.empty() ? 0.26f : v.layout.wheels[0].radius;
                v.layout.wheels.clear();
                for (const vec3& w : spec->wheels) v.layout.wheels.push_back({vec3(w.x, w.y > 0.0f ? w.y : r, w.z), r});
            }
            if (spec->has_seat) v.layout.seat = spec->seat;
        }
    if (gobby_kart) {
        // Its wheels sit wider, and the rear ones are bigger.
        const float rf = 0.24f * look.tires.size, rr = 0.27f * look.tires.size;
        v.layout.wheels = {{vec3(-0.62f, rf, 0.66f), rf}, {vec3(0.62f, rf, 0.66f), rf}, {vec3(-0.64f, rr, -0.66f), rr}, {vec3(0.64f, rr, -0.66f), rr}};
    }
    if (!v.glider_model) v.glider = renderer_.upload_mesh(render::make_glider_mesh(look.glider));
    return build_visuals_.emplace(key, std::move(v)).first->second;
}

void App::draw_model(const GpuModel& model, const mat4& m, vec4 tint) {
    for (render::MeshId id : model.meshes) renderer_.draw(id, m, tint);
}

void App::draw_kart_visual(const KartVisual& v, const mat4& m, vec4 tint, float wheel_angle) {
    const mat4 lifted = glm::translate(m, vec3(0, v.layout.lift, 0));
    if (v.body_model) draw_model(*v.body_model, lifted, tint);
    if (v.body) renderer_.draw(v.body, m, tint);
    if (v.driver_model) draw_model(*v.driver_model, glm::translate(m, v.layout.seat), tint);
    if (v.driver) renderer_.draw(v.driver, m, tint);
    if (v.tire_model) {
        for (const auto& w : v.layout.wheels) {
            const float ref = v.tire_model->bounds_max.y - v.tire_model->bounds_min.y;
            const float s = ref > 1e-6f ? 2.0f * w.radius / ref : 1.0f;
            mat4 t = glm::translate(m, w.center);
            if (w.center.x < -0.01f) t = glm::rotate(t, kPi, vec3(0, 1, 0)); // left wheels face out
            t = glm::rotate(t, w.center.x < -0.01f ? -wheel_angle : wheel_angle, vec3(1, 0, 0));
            draw_model(*v.tire_model, glm::scale(t, vec3(s)), tint);
        }
    }
    if (v.wheels) renderer_.draw(v.wheels, m, tint);
}

void App::draw_glider_visual(const KartVisual& v, const mat4& m, vec4 tint) {
    if (v.glider_model) draw_model(*v.glider_model, glm::translate(m, vec3(0, 1.0f + v.layout.lift, 0)), tint);
    else if (v.glider) renderer_.draw(v.glider, m, tint);
}

void App::set_kart_meshes(const std::vector<sim::Build>& builds) {
    kart_visuals_.clear();
    for (int i = 0; i < 12; ++i) {
        const sim::Build b = size_t(i) < builds.size() ? builds[size_t(i)] : sim::Build{i % int(parts_.drivers.size()), 0, 0, 0};
        kart_visuals_.push_back(build_visual(b, i));
    }
}

std::string App::race_track_id() const {
    // A --track the catalog doesn't list (such as "greybox") is built directly.
    if (!options_.track.empty() && tracks_.find(options_.track) < 0) return options_.track;
    if (course_.mode == GameMode::GrandPrix && !tracks_.cups.empty()) {
        const auto& cup = tracks_.cups[size_t(course_.cup % int(tracks_.cups.size()))];
        if (!cup.tracks.empty()) return cup.tracks[size_t(gp_.race % int(cup.tracks.size()))];
    }
    return course_track(course_, tracks_);
}

void App::show_track(const std::string& id) {
    if (shown_track_ && shown_track_->id == (id.empty() ? "greybox" : id)) return;
    shown_track_ = std::make_unique<sim::Track>(sim::make_track(tracks_, id));
    auto it = track_meshes_.find(shown_track_->id);
    if (it == track_meshes_.end()) {
        const render::MeshId track = renderer_.upload_mesh(render::make_track_mesh(*shown_track_, true));
        std::vector<render::PropPlacement> placements;
        const render::MeshId scenery = renderer_.upload_mesh(render::make_scenery_mesh(*shown_track_, &placements));
        // One mesh per kind of prop, all its copies merged.
        std::map<std::string, render::MeshBuilder> merged;
        for (const auto& p : placements) {
            auto pm = props_.find(p.name);
            if (pm == props_.end()) continue;
            mat4 m = p.transform;
            if (p.fit) { // model bounds -> x, z in -1..1, y in 0..1
                const vec3 lo = pm->second.lo, hi = pm->second.hi, size = glm::max(hi - lo, vec3(1e-4f));
                m = m * glm::scale(mat4(1.0f), vec3(2.0f / size.x, 1.0f / size.y, 2.0f / size.z)) *
                    glm::translate(mat4(1.0f), vec3(-0.5f * (lo.x + hi.x), -lo.y, -0.5f * (lo.z + hi.z)));
            }
            merged[p.name].append(pm->second.mesh, m);
        }
        std::vector<render::MeshId>& ids = track_props_[shown_track_->id];
        for (auto& [name, builder] : merged) {
            const PropModel& pm = props_.at(name);
            ids.push_back(renderer_.upload_mesh(builder.take(), pm.texture, pm.two_sided, pm.unlit, pm.blended, pm.glow));
        }
        {   // painted walls, rails, kerbs and flags
            const render::PaintedModel side = render::make_trackside(*shown_track_);
            const render::TextureId tex = renderer_.upload_texture(side.texture.size(), side.texture.size(), side.texture.rgba().data());
            ids.push_back(renderer_.upload_mesh(side.mesh, tex, false));
        }
        log_info("Track {}: {} props in {} meshes", shown_track_->id, placements.size(), ids.size());
        it = track_meshes_.emplace(shown_track_->id, std::pair(track, scenery)).first;
    }
    track_mesh_ = it->second.first;
    scenery_mesh_ = it->second.second;
    prop_meshes_ = &track_props_[shown_track_->id];
    const render::ThemeStyle& th = render::theme_style(shown_track_->theme);
    light_ = {};
    light_.sky = th.sky;
    light_.fog_density = th.fog_density;
    light_.ambient = th.ambient;
    if (shown_track_->theme == "night") { // moonlight: dim, blue, from high up
        light_.light_dir = glm::normalize(vec3(-0.35f, 0.8f, -0.45f));
        light_.sun = 0.32f;
        light_.ambient = 0.3f;
    }
}

void App::start_race() {
    sim::RaceConfig rc;
    rc.engine = course_.engine;
    const std::string id = race_track_id();
    show_track(id);
    sim::Track track = sim::make_track(tracks_, id);
    const bool time_trial = course_.mode == GameMode::TimeTrial;
    message_ = {track.name, render::rgba(255, 255, 255), 3.0f};
    auto clamp_part = [](int i, size_t n) { return std::clamp(i, 0, int(n) - 1); };
    options_.build.driver = clamp_part(options_.build.driver, parts_.drivers.size());
    options_.build.body = clamp_part(options_.build.body, parts_.bodies.size());
    options_.build.tires = clamp_part(options_.build.tires, parts_.tires.size());
    options_.build.glider = clamp_part(options_.build.glider, parts_.gliders.size());
    auto params_for = [&](const sim::Build& b) { return tables_.params(parts_.levels(b), parts_.body(b), rc.engine); };
    ghost_.reset();
    if (time_trial) {
        world_ = std::make_unique<sim::World>(sim::make_time_trial_world(std::move(track), tables_, rc, params_for(options_.build)));
        player_ = 0;
        recording_ = {.track = id, .name = "YOU", .engine = rc.engine, .laps = rc.laps, .build = options_.build};
        set_kart_meshes({options_.build});
        recording_done_ = false;
        // Race the personal best if it still replays on this version, else a CPU.
        sim::Ghost best;
        best_tick_ = 0;
        const std::string path = ghost_path(id, rc.engine);
        if (!path.empty() && sim::load_ghost(path, best) && best.track == id) {
            const sim::KartParams bp = params_for(best.build);
            if (sim::ghost_still_valid(best, sim::make_track(tracks_, id), tables_, bp)) {
                best_tick_ = best.finish_tick;
                ghost_build_ = best.build;
                ghost_ = std::make_unique<sim::GhostRunner>(sim::make_track(tracks_, id), tables_, rc, bp, &best);
            } else {
                log_info("Ghost {} no longer matches the game; racing a CPU ghost instead", path);
            }
        }
        if (!ghost_) {
            ghost_build_ = {3, 4, 2, 0}; // the CPU ghost drives a heavy racer
            ghost_ = std::make_unique<sim::GhostRunner>(sim::make_track(tracks_, id), tables_, rc, params_for(ghost_build_), nullptr);
        }
        ghost_prev_ = ghost_->kart();
    } else {
        world_ = std::make_unique<sim::World>(std::move(track), tables_, rc);
        const int karts = std::clamp(options_.karts, 1, 12);
        // The player starts at the back of the grid, as in a Grand Prix opener.
        player_ = karts - 1;
        std::vector<sim::Build> builds;
        for (int i = 0; i < karts; ++i) {
            sim::Build b = options_.build;
            if (i != player_) b = sim::cpu_build(parts_, i);
            sim::KartParams p = params_for(b);
            if (i != player_) p.rubber_band = sim::cpu_rubber_band(course_.cpu);
            world_->add_kart(p);
            builds.push_back(b);
        }
        set_kart_meshes(builds);
    }
    prev_ = world_->karts();
    cam_init_ = false;
}

std::string App::ghost_path(const std::string& track, sim::EngineClass e) const {
    if (pref_dir_.empty()) return {};
    return pref_dir_ + "ghost_" + track + "_" + engine_name(e) + ".gkg";
}

void App::finish_time_trial() {
    const sim::KartState& k = world_->karts()[size_t(player_)];
    recording_.finish_tick = k.finish_tick;
    recording_done_ = true;
    if (best_tick_ > 0 && k.finish_tick >= best_tick_) return;
    best_tick_ = k.finish_tick;
    const std::string path = ghost_path(recording_.track, recording_.engine);
    if (!path.empty() && sim::save_ghost(path, recording_)) log_info("New best: ghost saved to {}", path);
    message_ = {"NEW RECORD!", render::rgba(255, 220, 60), 3.0f};
}

void App::draw_ghost(float alpha) {
    if (!ghost_) return;
    const sim::KartState& s = ghost_->kart();
    const mat4 m = glm::rotate(glm::translate(mat4(1.0f), glm::mix(ghost_prev_.pos, s.pos, alpha)) *
                                   glm::mat4_cast(glm::slerp(ghost_prev_.rot, s.rot, alpha)),
                               -s.visual_drift_yaw, vec3(0, 1, 0));
    const KartVisual& v = build_visual(ghost_build_, 6);
    draw_kart_visual(v, m, vec4(0.7f, 0.85f, 1.0f, 0.45f), 0.0f);
    if (s.gliding) draw_glider_visual(v, m, vec4(0.7f, 0.85f, 1.0f, 0.45f));
}

void App::finish_offline_race() {
    const auto order = world_->standings();
    const auto names = kart_names();
    const bool gp = course_.mode == GameMode::GrandPrix;
    gp_.rows.clear();
    for (size_t p = 0; p < order.size(); ++p) {
        const int k = order[p];
        const sim::KartState& ks = world_->karts()[size_t(k)];
        const int pts = course_.mode == GameMode::TimeTrial ? 0 : net::kRacePoints[std::min<size_t>(p, 11)];
        if (gp) gp_.points[size_t(k)] += pts;
        gp_.rows.push_back({names[size_t(k)], ks.finished ? ks.finish_tick : 0, pts, gp ? gp_.points[size_t(k)] : -1,
                            k == player_});
    }
    int races = 4;
    if (gp && !tracks_.cups.empty()) races = int(tracks_.cups[size_t(course_.cup % int(tracks_.cups.size()))].tracks.size());
    gp_.over = gp && gp_.race + 1 >= races;
    if (gp_.over)
        std::stable_sort(gp_.rows.begin(), gp_.rows.end(), [](const ResultRow& a, const ResultRow& b) { return a.total > b.total; });
}

void App::draw_standings() {
    char title[64];
    const bool gp = course_.mode == GameMode::GrandPrix;
    if (gp_.over) std::snprintf(title, sizeof title, "GRAND PRIX RESULTS");
    else if (gp) std::snprintf(title, sizeof title, "RACE %d OF 4", gp_.race + 1);
    else std::snprintf(title, sizeof title, course_.mode == GameMode::TimeTrial ? "TIME TRIAL" : "RESULTS");
    std::vector<ResultRow> rows = gp_.rows;
    if (course_.mode == GameMode::TimeTrial)
        for (auto& r : rows) r.points = -1;
    draw_results(renderer_, title, rows, gp && !gp_.over ? "ENTER / A: NEXT RACE" : "ENTER / A: CONTINUE");
}

void App::update() {
    if (online_) {
        const sim::World* w = online_->world();
        if (online_->race_id_changed(online_race_)) {
            std::vector<sim::Build> builds;
            for (const auto& seat : online_->setup().seats) builds.push_back(seat.build);
            set_kart_meshes(builds);
            cam_init_ = false;
            message_ = {};
        }
        // Spectators follow the leader.
        player_ = online_->seat() >= 0 ? online_->seat() : (w && !w->karts().empty() ? w->standings()[0] : 0);
        if (w) prev_ = w->karts();
        sim::KartInput in = paused_ ? sim::KartInput{} : input_.sample(); // online the race goes on: let go of the pedals
        if (w && online_->seat() >= 0 && options_.autodrive) {
            in = net::bot_input(*w, player_, sim::CpuLevel::Hard);
        }
        last_input_ = in;
        online_->step(in);
        handle_events(online_->take_events());
        if (message_.time_left > 0.0f) message_.time_left -= kDt;
        return;
    }
    if (!world_ || screen_ != Screen::Race || paused_) return;
    prev_ = world_->karts();
    const auto& karts = world_->karts();
    std::vector<sim::KartInput> inputs(karts.size());
    for (size_t i = 0; i < karts.size(); ++i) {
        if (int(i) == player_ && !options_.autodrive) {
            inputs[i] = input_.sample();
        } else {
            inputs[i] = net::bot_input(*world_, int(i), int(i) == player_ ? sim::CpuLevel::Hard : options_.cpu_level);
        }
        if (int(i) == player_ && options_.spawn_obstacle >= 0) inputs[i] = sim::KartInput{.accelerate = world_->racing()};
    }
    if (ghost_) inputs[size_t(player_)] = sim::quantize(inputs[size_t(player_)]); // exactly what the ghost file holds
    last_input_ = inputs[size_t(player_)];
    if (ghost_ && !recording_done_) recording_.inputs.push_back(inputs[size_t(player_)]);
    world_->tick(inputs);
    handle_events(world_->events());
    if (ghost_) {
        ghost_prev_ = ghost_->kart();
        ghost_->tick();
        if (!recording_done_ && world_->karts()[size_t(player_)].finished) finish_time_trial();
    }
    if (message_.time_left > 0.0f) message_.time_left -= kDt;
}

void App::handle_events(const std::vector<sim::KartEvent>& events) {
    for (const auto& e : events) {
        event_sound(e);
        if (e.kart != player_) continue;
        switch (e.type) {
        case sim::KartEventType::Boost:
            input_.rumble(0.3f, 0.6f, 150);
            if (e.value == int(sim::BoostKind::RocketStart)) message_ = {"ROCKET START!", render::rgba(255, 200, 60), 1.5f};
            break;
        case sim::KartEventType::Trick: input_.rumble(0.2f, 0.2f, 80); break;
        case sim::KartEventType::Land: input_.rumble(0.5f, 0.2f, 120); break;
        case sim::KartEventType::WallHit: input_.rumble(0.8f, 0.4f, 120); break;
        case sim::KartEventType::Bump: input_.rumble(0.6f, 0.6f, 100); break;
        case sim::KartEventType::Smash: input_.rumble(0.5f, 0.3f, 90); break;
        case sim::KartEventType::TireKnock: input_.rumble(0.7f, 0.5f, 120); break;
        case sim::KartEventType::TireBounce: input_.rumble(0.3f, 0.6f, 120); break;
        case sim::KartEventType::Hit:
            input_.rumble(1.0f, 0.8f, 300);
            if ((e.value & 0xff) == int(sim::HitKind::Shrink)) flash_ = 1.0f;
            break;
        case sim::KartEventType::Blocked: input_.rumble(0.4f, 0.4f, 100); break;
        case sim::KartEventType::ItemGet: input_.rumble(0.1f, 0.3f, 60); break;
        case sim::KartEventType::Burnout: message_ = {"BURNOUT! (too early)", render::rgba(255, 90, 60), 2.0f}; break;
        case sim::KartEventType::Lap:
            message_ = {e.value == world()->config().laps ? "FINAL LAP!" : "LAP " + std::to_string(e.value),
                        render::rgba(255, 255, 255), 2.0f};
            break;
        default: break;
        }
    }
}

namespace {

// The account this install signed up with, per master server.
struct SavedAccount {
    std::string master, token;
};
SavedAccount load_account(const std::string& path) {
    SavedAccount a;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("master=", 0) == 0) a.master = line.substr(7);
        else if (line.rfind("token=", 0) == 0) a.token = line.substr(6);
    }
    return a;
}

} // namespace

void App::start_matchmaking() {
    screen_ = Screen::Online;
    online_error_.clear();
    const std::string url = !options_.master_url.empty() ? options_.master_url : settings_.master_url;
    if (url.empty()) {
        online_error_ = "NO MASTER SERVER SET (START WITH --master URL)";
        return;
    }
    if (matching_.valid()) return; // already searching
    const std::string account_file = pref_dir_.empty() ? std::string() : pref_dir_ + "account.txt";
    matching_ = std::async(std::launch::async, [url, account_file, name = options_.name, region = options_.region] {
        MatchResult r;
        online::MasterClient master(url);
        // Sign in with the saved token, or make an account the first time.
        const SavedAccount saved = account_file.empty() ? SavedAccount{} : load_account(account_file);
        std::optional<online::AccountInfo> me;
        if (saved.master == url && !saved.token.empty()) me = master.me(saved.token);
        if (!me) {
            me = master.create_account(name);
            if (!me) return MatchResult{"SIGN-UP FAILED: " + master.error()};
            if (!account_file.empty()) std::ofstream(account_file, std::ios::trunc) << "master=" << url << "\ntoken=" << me->token << "\n";
        }
        const auto server = master.match(region, net::kProtocolVersion);
        if (!server) return MatchResult{"NO RACE FOUND: " + master.error()};
        r.host = server->host;
        r.port = server->port;
        r.token = me->token;
        r.name = me->name;
        r.rating = me->rating;
        return r;
    });
}

void App::poll_matchmaking() {
    if (!matching_.valid() || matching_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    MatchResult r = matching_.get();
    if (screen_ != Screen::Online) return; // backed out meanwhile
    for (char& c : r.error) c = char(std::toupper(uint8_t(c)));
    if (!r.error.empty()) {
        online_error_ = r.error;
        return;
    }
    account_label_ = r.name + " (" + std::to_string(r.rating) + ")";
    options_.name = r.name;
    if (!connect_online(r.host, r.port, r.token)) online_error_ = "COULD NOT REACH " + r.host + ":" + std::to_string(r.port);
}

bool App::connect_online(const std::string& host, uint16_t port, const std::string& token) {
    if (!transport_ready_) {
        if (!net::transport_init()) return false;
        transport_ready_ = true;
    }
    net_ = std::make_unique<net::NetClient>();
    net_->set_conditions(options_.link);
    if (!net_->connect(host, port)) {
        net_.reset();
        return false;
    }
    online_ = std::make_unique<net::ClientGame>(tables_, [this](uint8_t ch, std::vector<uint8_t> d, bool rel) {
        net_->send(ch, std::move(d), rel);
    });
    online_->set_tracks(tracks_);
    online_->join(options_.name, select_.build, token);
    select_.ready = false;
    screen_ = Screen::Select;
    return true;
}

void App::start_join() {
    join_text_ = settings_.join_address;
    join_hint_.clear();
    screen_ = Screen::Join;
    SDL_StartTextInput(window_);
}

// The gk_server program that came with the game: next to it (Windows zip,
// a build folder) or inside the macOS app, else next to the app.
static std::string find_server_program() {
    namespace fs = std::filesystem;
    const char* base = SDL_GetBasePath();
    if (!base) return {};
#if defined(_WIN32)
    const char* exe = "gk_server.exe";
#else
    const char* exe = "gk_server";
#endif
    const fs::path b(base);
    for (const fs::path& p : {b / exe, b / ".." / "MacOS" / exe, b / ".." / ".." / ".." / exe, b / ".." / "server" / exe, b / ".." / ".." / "server" / exe}) {
        std::error_code ec;
        if (fs::is_regular_file(p, ec)) return fs::weakly_canonical(p, ec).string();
    }
    return {};
}

void App::start_hosting() {
    stop_hosting();
    screen_ = Screen::Hosting;
    host_failed_ = false;
    host_share_.clear();
    const std::string server = find_server_program();
    if (server.empty()) {
        host_failed_ = true;
        host_lines_ = {"THE SERVER PROGRAM (GK_SERVER) WASN'T FOUND NEXT TO THE GAME"};
        return;
    }
    const std::string dir = (pref_dir_.empty() ? std::string(".") : pref_dir_) + "server";
    const std::string info = dir + "/host.txt";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::filesystem::remove(info, ec);
    const std::string port = std::to_string(net::kDefaultPort);
#if defined(_WIN32)
    const std::string self_pid = std::to_string(_getpid());
#else
    const std::string self_pid = std::to_string(getpid());
#endif
    const char* args[] = {server.c_str(), "--port", port.c_str(), "--upnp", "--info-file", info.c_str(), "--workdir", dir.c_str(),
                          "--parent-pid", self_pid.c_str(), nullptr};
    // In the background, with no stdio: a windowed Windows game has no console
    // handles to inherit (DuplicateHandle fails), and this also keeps the
    // server from opening a console window of its own.
    const SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, const_cast<char**>(args));
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
    host_process_ = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    if (!host_process_) {
        host_failed_ = true;
        host_lines_ = {std::string("COULD NOT START THE SERVER: ") + SDL_GetError()};
        return;
    }
    host_started_ = double(SDL_GetTicks()) / 1000.0;
    host_lines_ = {"STARTING THE SERVER AND OPENING THE PORT ON YOUR ROUTER..."};
}

void App::poll_hosting() {
    if (screen_ != Screen::Hosting || !host_process_ || host_failed_) return;
    int code = 0;
    if (SDL_WaitProcess(host_process_, false, &code)) {
        SDL_DestroyProcess(host_process_);
        host_process_ = nullptr;
        host_failed_ = true;
        host_lines_ = {"THE SERVER STOPPED (IS PORT " + std::to_string(net::kDefaultPort) + " ALREADY IN USE?)"};
        return;
    }
    const std::string info = (pref_dir_.empty() ? std::string(".") : pref_dir_) + "server/host.txt";
    std::ifstream f(info);
    if (!f) {
        if (double(SDL_GetTicks()) / 1000.0 - host_started_ > 12.0) {
            host_failed_ = true;
            host_lines_ = {"THE SERVER DIDN'T START"};
        }
        return;
    }
    std::string line, lan, pub, upnp;
    while (std::getline(f, line)) {
        if (line.rfind("lan=", 0) == 0) lan = line.substr(4);
        else if (line.rfind("public=", 0) == 0) pub = line.substr(7);
        else if (line.rfind("upnp=", 0) == 0) upnp = line.substr(5);
    }
    if (upnp.empty()) return; // still being written
    // Share the best address: the internet one when the router forwarded it.
    const std::string share = upnp == "ok" && !pub.empty() ? pub : lan;
    if (!share.empty()) SDL_SetClipboardText(share.c_str());
    host_share_ = "FRIENDS JOIN WITH: " + share + (upnp == "ok" ? "  (COPIED)" : "  (SAME NETWORK ONLY, COPIED)");
    if (!connect_online("127.0.0.1", net::kDefaultPort, {})) {
        host_failed_ = true;
        host_lines_ = {"COULD NOT JOIN YOUR OWN SERVER"};
    }
}

void App::stop_hosting() {
    if (!host_process_) return;
    SDL_KillProcess(host_process_, false); // lets the server close the port on the router
    SDL_WaitProcess(host_process_, true, nullptr);
    SDL_DestroyProcess(host_process_);
    host_process_ = nullptr;
    host_share_.clear();
}

void App::leave_online() {
    stop_hosting();
    online_.reset();
    net_.reset();
    online_race_ = 0;
    screen_ = Screen::Title;
    show_track(course_track(course_, tracks_));
}

void App::apply_settings() {
    renderer_.set_full_resolution(!settings_.low_res);
    show_help_ = settings_.show_help;
    input_.rumble_enabled = settings_.rumble;
    SDL_SetWindowFullscreen(window_, settings_.fullscreen);
    audio_.mixer().set_volumes({settings_.master / 10.0f, settings_.music / 10.0f, settings_.sfx / 10.0f});
}

void App::play_at(audio::Sfx s, int kart, float pitch) {
    audio::Mixer& mx = audio_.mixer();
    const sim::World* w = world();
    if (!w || kart == player_ || kart < 0 || size_t(kart) >= w->karts().size() || !cam_init_) {
        mx.play(s, 1.0f, 0.0f, pitch);
        return;
    }
    // Other karts: quieter with distance, panned by where they are on screen.
    const vec4 v = glm::lookAt(cam_pos_, cam_look_, vec3(0, 1, 0)) * vec4(w->karts()[size_t(kart)].pos, 1.0f);
    const float d = glm::length(vec3(v));
    if (d > 90.0f) return;
    const float pan = std::clamp(v.x / (std::abs(v.z) + 4.0f), -1.0f, 1.0f) * 0.8f;
    mx.play(s, 0.8f / (1.0f + d / 12.0f), pan, pitch);
}

void App::event_sound(const sim::KartEvent& e) {
    using audio::Sfx;
    const bool mine = e.kart == player_;
    Sfx s = Sfx::Count;
    float pitch = 1.0f;
    switch (e.type) {
    case sim::KartEventType::Boost:
        switch (sim::BoostKind(e.value)) {
        case sim::BoostKind::MiniTurbo: s = Sfx::MiniTurbo; break;
        case sim::BoostKind::SuperMiniTurbo: s = Sfx::MiniTurbo, pitch = 1.15f; break;
        case sim::BoostKind::UltraMiniTurbo: s = Sfx::MiniTurbo, pitch = 1.3f; break;
        case sim::BoostKind::RocketStart: s = Sfx::RocketStart; break;
        case sim::BoostKind::Star: s = Sfx::Star; break;
        case sim::BoostKind::Trick: s = Sfx::MiniTurbo, pitch = 0.9f; break;
        default: s = Sfx::Boost; break;
        }
        break;
    case sim::KartEventType::Trick: s = Sfx::Trick; break;
    case sim::KartEventType::Land: s = Sfx::Land; break;
    case sim::KartEventType::WallHit: s = Sfx::WallHit; break;
    case sim::KartEventType::Bump: s = Sfx::Bump; break;
    case sim::KartEventType::Respawn: s = Sfx::Respawn; break;
    case sim::KartEventType::GliderOpen: s = Sfx::GliderOpen; break;
    case sim::KartEventType::Burnout: s = Sfx::Bump, pitch = 0.6f; break;
    case sim::KartEventType::ItemBox: s = Sfx::ItemBox; break;
    case sim::KartEventType::ItemGet: s = mine ? Sfx::ItemGet : Sfx::Count; break;
    case sim::KartEventType::ItemUse:
        switch (sim::Item(e.value)) {
        case sim::Item::Banana:
        case sim::Item::TripleBanana: s = Sfx::Drop; break;
        case sim::Item::Mushroom:
        case sim::Item::TripleMushroom:
        case sim::Item::GoldenMushroom:
        case sim::Item::Star: break; // the boost event sounds
        case sim::Item::Horn: s = Sfx::Horn; break;
        case sim::Item::Coin: s = Sfx::Coin; break;
        case sim::Item::Ink: s = Sfx::Ink; break;
        case sim::Item::Ghost: s = Sfx::Ghost; break;
        case sim::Item::Bullet: s = Sfx::RocketStart, pitch = 0.8f; break;
        case sim::Item::Lightning: audio_.mixer().play(Sfx::Lightning); return; // everyone hears it
        default: s = Sfx::Throw; break;
        }
        break;
    case sim::KartEventType::Hit: s = (e.value & 0xff) == int(sim::HitKind::Shrink) ? Sfx::Shrink : Sfx::Hit; break;
    case sim::KartEventType::Explosion: s = Sfx::Explosion; break;
    case sim::KartEventType::Blocked: s = Sfx::Blocked; break;
    case sim::KartEventType::Smash: s = Sfx::Crunch; break;
    case sim::KartEventType::TireKnock: s = Sfx::Bump, pitch = 0.75f; break;
    case sim::KartEventType::TireBounce: s = Sfx::Boing; break;
    case sim::KartEventType::Lap:
        if (mine && world()) s = e.value == world()->config().laps ? Sfx::FinalLap : Sfx::Lap;
        break;
    case sim::KartEventType::Finish: s = mine ? Sfx::Finish : Sfx::Count; break;
    default: break;
    }
    if (s != Sfx::Count) play_at(s, e.kart, pitch);
}

namespace {
uint32_t fnv1a(const std::string& s) {
    uint32_t h = 2166136261u;
    for (char c : s) h = (h ^ uint8_t(c)) * 16777619u;
    return h;
}
} // namespace

void App::update_audio() {
    audio::Mixer& mx = audio_.mixer();
    const sim::World* w = world();
    const bool in_race = (screen_ == Screen::Race || screen_ == Screen::Standings) && w && size_t(player_) < w->karts().size();
    if (!in_race) {
        mx.play_music(0x6b617274u, audio::Mood::Menu);
        mx.set_engine({});
        last_count_ = -1;
        return;
    }
    const sim::KartState& k = w->karts()[size_t(player_)];
    const sim::Track& t = w->track();
    const bool done = screen_ == Screen::Standings || k.finished ||
                      (online_ && online_->phase() == net::RoomPhase::Results);
    if (done) mx.play_music(fnv1a(t.id) + 1u, audio::Mood::Results);
    else if (k.star_frames > 0) mx.play_music(0x57a2u, audio::Mood::Star);
    else mx.play_music(fnv1a(t.id), audio::mood_for_theme(t.theme));
    mx.set_music_speed(!done && k.lap >= w->config().laps ? 1.1f : 1.0f);

    // Countdown beeps: 3, 2, 1, go.
    if (!w->racing()) {
        const int sec = (w->countdown_left() + 59) / 60;
        if (sec != last_count_) {
            if (sec >= 1 && sec <= 3) mx.play(audio::Sfx::CountBeep);
            last_count_ = sec;
        }
    } else if (last_count_ >= 1) {
        mx.play(audio::Sfx::CountGo);
        last_count_ = 0;
    }
    // Roulette ticks while the item spins.
    if (k.roulette > 0 && k.roulette / 5 != last_roulette_ / 5) mx.play(audio::Sfx::RouletteTick, 0.7f);
    last_roulette_ = k.roulette;

    audio::EngineState e;
    e.active = !done && screen_ == Screen::Race && !(paused_ && !online_);
    const float top = std::max(1.0f, w->params(player_).top_speed);
    e.speed = (k.grounded ? std::abs(k.speed) : glm::length(k.air_vel)) / top;
    e.throttle = last_input_.accelerate || k.bullet_frames > 0 ? 1.0f : last_input_.brake ? 0.3f : 0.0f;
    const auto& kc = w->tables().constants;
    e.drift_tier = k.drift == sim::DriftState::Drifting ? (k.mt_charge >= kc.spark_threshold ? k.mt_tier(kc) : 0) : -1;
    e.rough = k.surface == sim::Surface::OffroadLight ? 0.4f : k.surface == sim::Surface::OffroadMedium ? 0.7f
            : k.surface == sim::Surface::OffroadHeavy ? 1.0f : k.surface == sim::Surface::Sand ? 0.3f : 0.0f;
    e.airborne = !k.grounded;
    e.boosting = k.boost_frames > 0;
    mx.set_engine(e);
}

void App::update_camera(float alpha, float dt) {
    const sim::KartState& now = world()->karts()[size_t(player_)];
    const sim::KartState& prev = prev_.size() > size_t(player_) ? prev_[size_t(player_)] : now;
    // Follow the kart as drawn (interpolated, with the network correction),
    // so the camera never jitters against the kart.
    vec3 pos = glm::mix(prev.pos, now.pos, alpha);
    if (online_) pos += online_->visual_offset(player_);
    const vec3 fwd = glm::slerp(prev.rot, now.rot, alpha) * vec3(0, 0, 1);
    vec3 heading = vec3(fwd.x, 0.0f, fwd.z);
    heading = glm::length(heading) > 0.01f ? glm::normalize(heading) : vec3(0, 0, 1);
    const bool back = input_.actions().look_back;
    const float dist = now.gliding ? 7.5f : 5.5f;
    const float height = now.gliding ? 3.2f : 2.2f;
    if (!cam_init_) {
        cam_dir_ = heading;
        cam_height_ = height;
        cam_dist_ = dist;
    } else {
        // Swing round to the heading by angle, not by sliding the position,
        // so turns and drifts never pull the camera through the kart.
        const float k = 1.0f - std::exp(-7.0f * dt);
        const float cross = cam_dir_.x * heading.z - cam_dir_.z * heading.x;
        const float ang = std::atan2(cross, glm::dot(cam_dir_, heading));
        const float turn = ang * k;
        const float c = std::cos(turn), sn = std::sin(turn);
        cam_dir_ = glm::normalize(vec3(cam_dir_.x * c - cam_dir_.z * sn, 0.0f, cam_dir_.x * sn + cam_dir_.z * c));
        const float k2 = 1.0f - std::exp(-4.0f * dt);
        cam_height_ += (height - cam_height_) * k2;
        cam_dist_ += (dist - cam_dist_) * k2;
    }
    // Looking back is a clean cut both ways, then follows just as smoothly.
    const vec3 dir = back ? -cam_dir_ : cam_dir_;
    const vec3 cam_target = pos - dir * cam_dist_ + kUp * cam_height_;
    if (!cam_init_ || back != cam_back_) cam_y_ = cam_target.y;
    else cam_y_ += (cam_target.y - cam_y_) * (1.0f - std::exp(-10.0f * dt)); // soften hops and bumps
    cam_pos_ = vec3(cam_target.x, cam_y_, cam_target.z);
    cam_look_ = pos + dir * 3.0f + kUp * 0.9f;
    cam_back_ = back;
    cam_init_ = true;
    const float target_fov = now.boost_frames > 0 ? 82.0f : 72.0f;
    cam_fov_ += (target_fov - cam_fov_) * (1.0f - std::exp(-5.0f * dt));
}

void App::draw_kart(int index, const sim::KartState& s, float alpha) {
    const sim::KartState& p = prev_.size() > size_t(index) ? prev_[size_t(index)] : s;
    vec3 pos = glm::mix(p.pos, s.pos, alpha);
    if (online_) pos += online_->visual_offset(index);
    const quat rot = glm::slerp(p.rot, s.rot, alpha);
    // Bumping down the stairs: a hop on every step.
    if (s.grounded && world() && world()->track().on_stairs(world()->track().center[size_t(s.sample)].s)) {
        const float along = glm::dot(pos - world()->track().center[size_t(s.sample)].pos, world()->track().center[size_t(s.sample)].tangent) +
                            world()->track().center[size_t(s.sample)].s;
        pos.y += 0.09f * std::abs(std::sin(along / 1.2f * kPi));
    }
    mat4 m = glm::translate(mat4(1.0f), pos) * glm::mat4_cast(rot);
    m = glm::rotate(m, -s.visual_drift_yaw, vec3(0, 1, 0));
    if (s.trick_anim > 0.0f) m = glm::rotate(m, (1.0f - s.trick_anim) * 2.0f * kPi, vec3(0, 0, 1));
    if (s.drift == sim::DriftState::Drifting) m = glm::rotate(m, float(s.drift_dir) * 0.08f, vec3(0, 0, 1));
    if (s.wall_bump > 0.0f) m = glm::rotate(m, std::sin(float(frame_) * 1.7f) * 0.08f * s.wall_bump, vec3(0, 1, 0));
    const mat4 base = m; // before hit animations, for held items
    if (s.spin_frames > 0) m = glm::rotate(m, float(s.spin_frames) * 0.42f, vec3(0, 1, 0));
    if (s.tumble_frames > 0) {
        const float t = 1.0f - float(s.tumble_frames) / float(world()->item_tuning().tumble_frames);
        m = glm::translate(m, vec3(0, std::sin(t * kPi) * 1.2f, 0));
        m = glm::rotate(m, t * 2.0f * kPi, vec3(1, 0, 0));
    }
    if (s.shrink_frames > 0) m = glm::scale(m, vec3(0.55f));
    vec4 tint(1.0f);
    if (s.respawn_frames > 0 || (s.invincible_frames > 0 && (frame_ / 4) % 2)) tint = vec4(1, 1, 1, 1) * 0.6f + 0.4f;
    if (s.star_frames > 0) {
        const float h = float(frame_) * 0.3f;
        tint = vec4(1.0f + 0.6f * std::sin(h), 1.0f + 0.6f * std::sin(h + 2.1f), 1.0f + 0.6f * std::sin(h + 4.2f), 1.0f);
    }
    if (s.ghost_frames > 0) tint = vec4(index == player_ ? 0.45f : 0.12f);
    const KartVisual& visual = kart_visuals_[size_t(index % 12)];
    if (s.grounded && s.respawn_frames == 0)
        renderer_.draw(shadow_mesh_, glm::scale(glm::translate(base, vec3(0, 0.03f, -0.02f)), vec3(1.15f, 1.0f, 1.55f)));
    if (!visual.layout.wheels.empty() && index < 12)
        wheel_angle_[size_t(index)] += s.speed / visual.layout.wheels[0].radius * frame_dt_;
    if (s.bullet_frames > 0) draw_item_mesh(bullet_mesh_, "bullet", base);
    else draw_kart_visual(visual, m, tint, index < 12 ? wheel_angle_[size_t(index)] : 0.0f);
    if (s.chomper_frames > 0) {
        const float lunge = s.chomper_cooldown > world()->item_tuning().chomper_cooldown - 10 ? 0.8f : 0.0f;
        draw_item_mesh(chomper_mesh_, "chomper", glm::translate(base, vec3(0, 0.3f, 1.1f + lunge)));
    }
    if (s.item != sim::Item::None && s.roulette == 0 && s.bullet_frames == 0)
        draw_held_item(s.item, s.item_uses, base, s.trailing);
    if (s.gliding) draw_glider_visual(visual, m, tint);

    const auto& kc = world()->tables().constants;
    if (s.drift == sim::DriftState::Drifting && s.mt_charge >= kc.spark_threshold) {
        const vec4 c = to_tint(tier_color(s.mt_tier(kc)));
        for (float side : {-1.0f, 1.0f}) {
            const float j = std::sin(float(frame_ + index) * 2.3f + side) * 0.08f;
            renderer_.draw(spark_mesh_, glm::translate(m, vec3(side * 0.55f, 0.12f + j, -0.7f - j)), c * 1.3f);
        }
    }
    if (s.boost_frames > 0) {
        const float flicker = 0.8f + 0.2f * std::sin(float(frame_) * 1.9f + float(index));
        vec4 c = to_tint(render::rgba(255, 170, 40));
        if (s.boost == sim::BoostKind::MiniTurbo) c = to_tint(tier_color(1));
        if (s.boost == sim::BoostKind::SuperMiniTurbo) c = to_tint(tier_color(2));
        if (s.boost == sim::BoostKind::UltraMiniTurbo) c = to_tint(tier_color(3));
        renderer_.draw(flame_mesh_, glm::scale(glm::translate(m, vec3(0, 0.28f, -0.95f)), vec3(1.0f, 1.0f, flicker)), c * 1.4f);
    }
}

render::MeshId App::item_mesh(sim::Item item) const {
    switch (item) {
    case sim::Item::Banana: case sim::Item::TripleBanana: return banana_mesh_;
    case sim::Item::GreenShell: case sim::Item::TripleGreen: return green_mesh_;
    case sim::Item::RedShell: case sim::Item::TripleRed: return red_mesh_;
    case sim::Item::Bomb: return bomb_mesh_;
    default: return 0;
    }
}

const char* App::item_name(sim::Item item) {
    switch (item) {
    case sim::Item::Banana: case sim::Item::TripleBanana: return "banana";
    case sim::Item::GreenShell: case sim::Item::TripleGreen: return "green_shell";
    case sim::Item::RedShell: case sim::Item::TripleRed: return "red_shell";
    case sim::Item::Bomb: return "bomb";
    default: return "";
    }
}

void App::draw_item_mesh(render::MeshId mesh, const char* name, const mat4& m, vec4 tint) {
    if (const GpuModel* model = assets_.item(name)) draw_model(*model, m, tint);
    else if (mesh) renderer_.draw(mesh, m, tint);
}

void App::draw_held_item(sim::Item item, int uses, const mat4& kart, bool trailing) {
    const render::MeshId mesh = item_mesh(item);
    if (!mesh) return;
    const bool triple = item == sim::Item::TripleBanana || item == sim::Item::TripleGreen || item == sim::Item::TripleRed;
    if (triple) {
        // Orbiting the kart.
        for (int i = 0; i < uses; ++i) {
            const float a = float(frame_) * 0.08f + float(i) * 2.0f * kPi / 3.0f;
            draw_item_mesh(mesh, item_name(item), glm::translate(kart, vec3(std::cos(a) * 1.3f, 0.2f, std::sin(a) * 1.3f)));
        }
    } else if (trailing) {
        draw_item_mesh(mesh, item_name(item), glm::translate(kart, vec3(0, 0.05f, -1.4f)));
    }
}

render::MeshId App::single_prop(const std::string& name) {
    if (auto it = single_props_.find(name); it != single_props_.end()) return it->second;
    const auto pm = props_.find(name);
    if (pm == props_.end()) return single_props_[name] = 0;
    const PropModel& m = pm->second;
    return single_props_[name] = renderer_.upload_mesh(m.mesh, m.texture, m.two_sided, m.unlit, m.blended, m.glow);
}

mat4 App::fit_prop(const std::string& name, const mat4& transform) const {
    const auto pm = props_.find(name);
    if (pm == props_.end()) return transform;
    // Model bounds -> x, z in -1..1, y in 0..1, as for placed scenery.
    const vec3 lo = pm->second.lo, hi = pm->second.hi, size = glm::max(hi - lo, vec3(1e-4f));
    return transform * glm::scale(mat4(1.0f), vec3(2.0f / size.x, 1.0f / size.y, 2.0f / size.z)) *
           glm::translate(mat4(1.0f), vec3(-0.5f * (lo.x + hi.x), -lo.y, -0.5f * (lo.z + hi.z)));
}

void App::draw_breakables(const sim::World& w) {
    using B = sim::Track::Breakable;
    const auto& obstacles = w.track().obstacles;
    const auto& props = w.props();
    if (prop_seen_down_.size() != obstacles.size()) {
        prop_seen_down_.assign(obstacles.size(), 0);
        prop_pop_.assign(obstacles.size(), 10.0f);
        debris_.clear();
    }
    if (!chip_mesh_) {
        render::MeshBuilder b;
        b.box(vec3(0.0f), vec3(0.5f), render::rgba(255, 255, 255));
        chip_mesh_ = renderer_.upload_mesh(b.take());
    }
    auto rnd = [&] {
        debris_seed_ ^= debris_seed_ << 13;
        debris_seed_ ^= debris_seed_ >> 17;
        debris_seed_ ^= debris_seed_ << 5;
        return float(debris_seed_ & 0xffff) / 65535.0f;
    };
    for (size_t i = 0; i < obstacles.size() && i < props.size(); ++i) {
        const sim::Track::Obstacle& o = obstacles[i];
        if (o.breakable == B::None) continue;
        const sim::PropState& p = props[i];
        // Just broke: a burst of splinters. Just came back: pop up.
        if (p.down > 0 && prop_seen_down_[i] == 0 && o.breakable != B::Tires) {
            const vec4 tint = o.breakable == B::Crate ? vec4(0.75f, 0.55f, 0.33f, 1.0f)
                            : o.breakable == B::Barrel ? (i % 2 ? vec4(0.82f, 0.24f, 0.2f, 1.0f) : vec4(0.24f, 0.43f, 0.78f, 1.0f))
                                                       : vec4(0.55f, 0.9f, 1.3f, 1.0f);
            const vec3 centre = o.pos + o.up * (o.half.y + 0.3f);
            for (int k = 0; k < 16; ++k) {
                const float a = rnd() * 2.0f * kPi;
                Debris d;
                d.pos = centre + vec3(std::cos(a), 0.0f, std::sin(a)) * (0.5f * rnd() * o.half.x);
                d.vel = vec3(std::cos(a) * (3.0f + 5.0f * rnd()), 4.0f + 6.0f * rnd(), std::sin(a) * (3.0f + 5.0f * rnd()));
                d.axis = glm::normalize(vec3(rnd() - 0.5f, rnd() - 0.5f, rnd() - 0.5f) + vec3(0.01f));
                d.spin = 6.0f + 10.0f * rnd();
                d.life = 1.0f + 0.6f * rnd();
                d.size = 0.09f + 0.16f * rnd();
                d.tint = tint;
                debris_.push_back(d);
            }
        }
        if (p.down == 0 && prop_seen_down_[i] > 0) prop_pop_[i] = 0.0f;
        prop_seen_down_[i] = p.down;
        prop_pop_[i] += frame_dt_;

        const float h = 2.0f * o.half.y + 0.3f;
        if (p.down == 0) {
            // Pop back in with a little overshoot.
            const float t = std::min(1.0f, prop_pop_[i] / 0.35f);
            const float pop = t >= 1.0f ? 1.0f : 1.0f - std::cos(t * 4.0f) * (1.0f - t) * 0.9f;
            if (o.breakable == B::Crate) {
                // A stack of crates fitted to the block: rows, columns and layers of about a metre.
                const int nx = std::max(1, int(std::round(2.0f * o.half.x / 1.15f)));
                const int nz = std::max(1, int(std::round(2.0f * o.half.z / 1.15f)));
                const int ny = std::max(1, int(std::round(h / 1.05f)));
                const vec3 cell(2.0f * o.half.x / float(nx), h / float(ny), 2.0f * o.half.z / float(nz));
                for (int y = 0; y < ny; ++y)
                    for (int x = 0; x < nx; ++x)
                        for (int z = 0; z < nz; ++z) {
                            if (y > 0 && (x + z + y) % 3 == 0) continue; // stepped top
                            const vec3 c = o.pos + o.right * (-o.half.x + cell.x * (float(x) + 0.5f)) +
                                           o.forward * (-o.half.z + cell.z * (float(z) + 0.5f)) + o.up * (cell.y * float(y));
                            const float yaw = 0.12f * std::sin(float(i * 7 + x * 3 + z * 5 + y));
                            const quat q = glm::angleAxis(yaw, o.up);
                            const vec3 r = q * o.right, f = q * o.forward;
                            const mat4 m(vec4(r * (0.48f * cell.x * pop), 0.0f), vec4(o.up * (0.98f * cell.y * pop), 0.0f),
                                         vec4(f * (0.48f * cell.z * pop), 0.0f), vec4(c, 1.0f));
                            renderer_.draw(single_prop("crate"), fit_prop("crate", m));
                        }
            } else {
                const char* name = o.breakable == B::Barrel ? (i % 2 ? "barrel_red" : "barrel")
                                 : o.breakable == B::Crystal ? "crystal" : "tire_stack";
                const mat4 m(vec4(o.right * (o.half.x * pop), 0.0f), vec4(o.up * (h * pop), 0.0f), vec4(o.forward * (o.half.z * pop), 0.0f),
                             vec4(o.pos, 1.0f));
                renderer_.draw(single_prop(name), fit_prop(name, m));
            }
        } else if (o.breakable == B::Tires) {
            const float r = 0.62f;
            for (int k = 0; k < sim::kLooseTires; ++k) {
                if (p.used >> k & 1) continue;
                const vec3 at = w.loose_tire_pos(int(i), k) - o.up * 0.3f;
                // Rolls out on its side, then flops flat and settles.
                const float t = std::min(1.0f, float(p.down) / 32.0f);
                const float tilt = (1.0f - t) * 1.2f + 0.08f * std::sin(float(k) * 2.0f + float(p.down) * 0.2f) * (1.0f - t);
                mat4 m(vec4(o.right * r, 0.0f), vec4(o.up * 0.5f, 0.0f), vec4(o.forward * r, 0.0f), vec4(at, 1.0f));
                m = glm::rotate(m, tilt, vec3(1, 0, 0));
                renderer_.draw(single_prop("tire"), fit_prop("tire", m));
            }
        }
    }
    // Splinters: fall, bounce once off the road height they started from, fade.
    for (Debris& d : debris_) {
        d.vel.y -= 22.0f * frame_dt_;
        d.pos += d.vel * frame_dt_;
        d.angle += d.spin * frame_dt_;
        d.life -= frame_dt_;
        const float s = d.size * std::clamp(d.life / 0.3f, 0.0f, 1.0f);
        mat4 m = glm::rotate(glm::translate(mat4(1.0f), d.pos), d.angle, d.axis);
        renderer_.draw(chip_mesh_, glm::scale(m, vec3(s, s * 0.3f, s * 2.2f)), d.tint);
    }
    std::erase_if(debris_, [](const Debris& d) { return d.life <= 0.0f; });
}

void App::draw_items(const sim::World& w) {
    const auto& boxes = w.track().item_boxes;
    const auto& respawn = w.box_respawn();
    const float spin = float(frame_) * 0.03f;
    for (size_t i = 0; i < boxes.size() && i < respawn.size(); ++i) {
        const int r = respawn[i];
        if (r > 20) continue;
        const float scale = r > 0 ? 1.0f - float(r) / 20.0f : 1.0f;
        const float bob = std::sin(spin * 2.0f + float(i)) * 0.15f;
        mat4 m = glm::translate(mat4(1.0f), boxes[i] + vec3(0, bob, 0));
        m = glm::rotate(m, spin + float(i), vec3(0, 1, 0));
        m = glm::rotate(m, 0.5f, vec3(1, 0, 1));
        draw_item_mesh(box_mesh_, "item_box", glm::scale(m, vec3(scale)));
    }
    for (const sim::ItemEntity& e : w.entities()) {
        mat4 m = glm::translate(mat4(1.0f), e.pos - vec3(0, 0.3f, 0));
        const float yaw = std::atan2(e.vel.x, e.vel.z);
        switch (e.type) {
        case sim::EntityType::Banana: draw_item_mesh(banana_mesh_, "banana", m); break;
        case sim::EntityType::GreenShell: draw_item_mesh(green_mesh_, "green_shell", glm::rotate(m, float(e.age) * 0.4f, vec3(0, 1, 0))); break;
        case sim::EntityType::RedShell: draw_item_mesh(red_mesh_, "red_shell", glm::rotate(m, float(e.age) * 0.4f, vec3(0, 1, 0))); break;
        case sim::EntityType::SpikedShell:
            draw_item_mesh(spiked_mesh_, "spiked_shell", glm::rotate(glm::scale(m, vec3(1.4f)), float(e.age) * 0.3f, vec3(0, 1, 0)));
            break;
        case sim::EntityType::Bomb: {
            // Flashes faster as the fuse runs down.
            const bool lit = e.life > 0 && (e.life < 40 ? (e.age / 3) % 2 : (e.age / 10) % 2);
            draw_item_mesh(bomb_mesh_, "bomb", glm::rotate(m, yaw, vec3(0, 1, 0)), lit ? vec4(2.5f, 1.2f, 1.0f, 1.0f) : vec4(1.0f));
            break;
        }
        case sim::EntityType::Fireball:
            draw_item_mesh(ball_mesh_, "fireball", glm::translate(m, vec3(0, 0.3f, 0)), vec4(1.6f, 0.7f, 0.2f, 1.0f));
            break;
        case sim::EntityType::Boomerang:
            draw_item_mesh(boomerang_mesh_, "boomerang", glm::rotate(glm::translate(m, vec3(0, 0.3f, 0)), float(e.age) * 0.6f, vec3(0, 1, 0)));
            break;
        case sim::EntityType::Explosion: {
            const float t = 1.0f - float(e.life) / 30.0f;
            const float r = w.item_tuning().explosion_radius * (0.3f + 0.7f * t) / 0.3f;
            renderer_.draw(ball_mesh_, glm::scale(glm::translate(m, vec3(0, 0.3f, 0)), vec3(r)),
                           vec4(1.8f, 1.0f - 0.5f * t, 0.3f, 1.0f));
            break;
        }
        }
    }
}

void App::render(float alpha, float dt) {
    frame_dt_ = std::clamp(dt, 0.0f, 0.1f);
    if (screen_ == Screen::Title || screen_ == Screen::Options || screen_ == Screen::Online || screen_ == Screen::Course ||
        screen_ == Screen::OnlineMenu || screen_ == Screen::Join || screen_ == Screen::Hosting ||
        screen_ == Screen::Select) {
        render_menu(dt);
        return;
    }
    const sim::World* w = world();
    if (w && size_t(player_) < w->karts().size()) update_camera(alpha, dt);
    render::Camera cam;
    cam.position = cam_init_ ? cam_pos_ : vec3(0, 12, -25);
    cam.target = cam_init_ ? cam_look_ : vec3(0, 0, 20);
    cam.fov_y_deg = cam_fov_;
    if (w) show_track(w->track().id);
    if (!renderer_.begin_frame(cam, light_)) return;
    renderer_.draw(track_mesh_, mat4(1.0f));
    renderer_.draw(scenery_mesh_, mat4(1.0f));
    if (prop_meshes_)
        for (render::MeshId id : *prop_meshes_) renderer_.draw(id, mat4(1.0f));
    if (w && screen_ == Screen::Standings) {
        for (size_t i = 0; i < w->karts().size(); ++i) draw_kart(int(i), w->karts()[i], alpha);
        draw_standings();
    } else if (w) {
        const auto& karts = w->karts();
        draw_items(*w);
        draw_breakables(*w);
        for (size_t i = 0; i < karts.size(); ++i) draw_kart(int(i), karts[i], alpha);
        if (!online_) draw_ghost(alpha); // translucent, so after the solid karts
        const bool server_results = online_ && online_->results() && online_->phase() == net::RoomPhase::Results;
        draw_hud(renderer_, *w, player_, kart_names(), message_, show_help_ && !server_results && !paused_, !server_results);
        if (server_results) draw_online_results();
        if (ghost_) {
            char buf[64];
            if (best_tick_ > 0) {
                const int ms = best_tick_ * 1000 / 60;
                std::snprintf(buf, sizeof buf, "BEST %d:%02d.%03d", ms / 60000, ms / 1000 % 60, ms % 1000);
            } else {
                std::snprintf(buf, sizeof buf, "GHOST: CPU");
            }
            renderer_.ui_text(6.0f, 44.0f, buf, render::rgba(170, 215, 255), 1.5f);
        }
        if (!online_ && karts[size_t(player_)].finished) {
            const char* hint = "ENTER / A: CONTINUE";
            const VkExtent2D ext = renderer_.ui_extent();
            renderer_.ui_text((float(ext.width) - render::Renderer::ui_text_width(hint, 1.5f)) * 0.5f,
                              float(ext.height) - 22.0f, hint, render::rgba(255, 255, 255), 1.5f);
        }
    }
    if (flash_ > 0.0f) {
        const VkExtent2D ext = renderer_.ui_extent();
        renderer_.ui_rect(0, 0, float(ext.width), float(ext.height), render::rgba(255, 255, 230, uint8_t(flash_ * 200.0f)));
        flash_ = std::max(0.0f, flash_ - dt * 3.0f);
    }
    if (paused_ && screen_ == Screen::Race) draw_pause();
    if (online_) draw_net_status();
    renderer_.end_frame();
}

std::vector<std::string> App::kart_names() const {
    std::vector<std::string> names;
    const sim::World* w = world();
    if (!w) return names;
    for (size_t i = 0; i < w->karts().size(); ++i) {
        if (online_ && i < online_->setup().seats.size()) names.push_back(online_->setup().seats[i].name);
        else if (int(i) == player_) names.push_back("YOU");
        else names.push_back(parts_.drivers[i % parts_.drivers.size()].name);
    }
    return names;
}

std::vector<const char*> App::pause_items() const {
    if (!online_) return {"RESUME", "RESTART", "QUIT RACE", "QUIT GAME"};
    return {"RESUME", host_process_ ? "END SERVER" : "LEAVE RACE", "QUIT GAME"};
}

bool App::update_pause(const ClientActions& act, bool& running) {
    if (!paused_) {
        if (!act.pause_pressed) return false;
        paused_ = true;
        pause_row_ = 0;
        audio_.mixer().play(audio::Sfx::Pause);
        return true;
    }
    const auto items = pause_items();
    const int n = int(items.size());
    if (act.menu_up) pause_row_ = (pause_row_ + n - 1) % n;
    if (act.menu_down) pause_row_ = (pause_row_ + 1) % n;
    if (act.menu_up || act.menu_down) audio_.mixer().play(audio::Sfx::MenuMove);
    if (act.pause_pressed || act.back) {
        paused_ = false;
        audio_.mixer().play(audio::Sfx::MenuBack);
        return true;
    }
    if (!act.accept) return true;
    audio_.mixer().play(audio::Sfx::MenuAccept);
    const std::string choice = items[size_t(pause_row_)];
    paused_ = false;
    if (choice == "RESTART") {
        start_race();
    } else if (choice == "QUIT RACE") {
        screen_ = options_.track.empty() ? Screen::Course : Screen::Title;
        options_.track.clear();
        cam_init_ = false;
        show_track(course_track(course_, tracks_));
    } else if (choice == "LEAVE RACE" || choice == "END SERVER") {
        if (options_.connect_host.empty() || host_process_) leave_online();
        else running = false; // started with --connect: there is no menu to go back to
    } else if (choice == "QUIT GAME") {
        running = false;
    }
    return true;
}

void App::draw_pause() {
    const float W = ui::W(renderer_), H = ui::H(renderer_);
    renderer_.ui_rect(0, 0, W, H, render::rgba(6, 10, 30, 130));
    const auto items = pause_items();
    const int n = int(items.size());
    const float bw = 190.0f, top = H * 0.3f;
    ui::panel(renderer_, W * 0.5f - bw * 0.5f - 16.0f, top - 50.0f, bw + 32.0f, 62.0f + 27.0f * float(n), 14.0f);
    ui::title(renderer_, W * 0.5f, top - 38.0f, "PAUSED", 3.0f);
    if (online_) ui::text_c(renderer_, W * 0.5f, top - 12.0f, "The race carries on online", ui::kSubtle, 1.0f);
    ui::button_list(renderer_, W * 0.5f, top + 4.0f, bw, items.data(), n, pause_row_);
    ui::footer(renderer_, "UP/DOWN  CHOOSE      ENTER / A  OK      ESC / START  RESUME");
}

void App::handle_menu_actions(const ClientActions& act, bool& running) {
    if (online_) {
        // The server's phase decides the screen: lobby -> select, otherwise race.
        const bool lobby = online_->has_lobby() ? online_->phase() == net::RoomPhase::Lobby : !online_->world();
        const Screen want = lobby ? Screen::Select : Screen::Race;
        if (want != screen_) {
            screen_ = want;
            select_.ready = false;
        }
        if (screen_ != Screen::Race) paused_ = false;
        if (screen_ == Screen::Race) {
            update_pause(act, running);
            return;
        }
        if (options_.autodrive && !select_.ready && online_->has_lobby()) {
            select_.ready = true; // demo mode readies up by itself
            online_->set_build(select_.build, true);
        }
        switch (update_select(select_, act, parts_)) {
        case SelectAction::Changed: online_->set_build(select_.build, select_.ready); break;
        case SelectAction::Accept:
            select_.ready = !select_.ready;
            online_->set_build(select_.build, select_.ready);
            break;
        case SelectAction::Back:
            if (select_.ready) {
                select_.ready = false;
                online_->set_build(select_.build, false);
            } else if (options_.connect_host.empty()) {
                leave_online(); // back to the title menu
            } else {
                running = false;
            }
            break;
        default: break;
        }
        return;
    }
    switch (screen_) {
    case Screen::Title:
        switch (update_title(title_, act)) {
        case TitleChoice::GrandPrix: course_.mode = GameMode::GrandPrix; break;
        case TitleChoice::Versus: course_.mode = GameMode::Versus; break;
        case TitleChoice::TimeTrial: course_.mode = GameMode::TimeTrial; break;
        case TitleChoice::Options: screen_ = Screen::Options; return;
        case TitleChoice::Online: screen_ = Screen::OnlineMenu; return;
        case TitleChoice::Quit: running = false; return;
        case TitleChoice::None: return;
        }
        course_.row = 0;
        screen_ = Screen::Course;
        show_track(course_track(course_, tracks_));
        return;
    case Screen::OnlineMenu:
        switch (update_online_menu(online_menu_, act)) {
        case OnlineChoice::Host: start_hosting(); break;
        case OnlineChoice::Join: start_join(); break;
        case OnlineChoice::QuickMatch: start_matchmaking(); break;
        case OnlineChoice::Back: screen_ = Screen::Title; break;
        case OnlineChoice::None: break;
        }
        return;
    case Screen::Join: {
        if (act.back && !act.erase) {
            SDL_StopTextInput(window_);
            screen_ = Screen::OnlineMenu;
            return;
        }
        if (act.erase && !join_text_.empty()) join_text_.pop_back();
        std::string add = act.text;
        if (act.paste)
            if (char* clip = SDL_GetClipboardText()) {
                add += clip;
                SDL_free(clip);
            }
        for (char c : add)
            if ((std::isalnum(uint8_t(c)) || c == '.' || c == ':' || c == '-' || c == '[' || c == ']') && join_text_.size() < 60) join_text_ += c;
        if (act.enter && !join_text_.empty()) {
            std::string host = join_text_;
            uint16_t port = net::kDefaultPort;
            if (const size_t colon = host.rfind(':'); colon != std::string::npos && host.find(':') == colon) {
                port = uint16_t(std::atoi(host.c_str() + colon + 1));
                host = host.substr(0, colon);
            }
            settings_.join_address = join_text_;
            if (const std::string sp = settings_path(); !sp.empty()) settings_.save(sp);
            SDL_StopTextInput(window_);
            if (!connect_online(host, port, {})) {
                join_hint_ = "COULD NOT REACH " + join_text_;
                SDL_StartTextInput(window_);
            }
        }
        return;
    }
    case Screen::Hosting:
        if (act.back) {
            stop_hosting();
            screen_ = Screen::OnlineMenu;
        }
        return;
    case Screen::Online:
        if (act.back) {
            online_error_.clear();
            screen_ = Screen::OnlineMenu; // a running search finishes in the background and is dropped
        } else if (act.accept && !online_error_.empty()) {
            start_matchmaking(); // try again
        }
        return;
    case Screen::Options:
        switch (update_options(options_menu_, settings_, act)) {
        case OptionsAction::Changed: apply_settings(); break;
        case OptionsAction::Back:
            if (const std::string sp = settings_path(); !sp.empty()) settings_.save(sp);
            screen_ = Screen::Title;
            break;
        case OptionsAction::None: break;
        }
        return;
    case Screen::Course:
        switch (update_course(course_, act, tracks_)) {
        case CourseAction::Changed: show_track(course_track(course_, tracks_)); break;
        case CourseAction::Accept:
            gp_ = {};
            screen_ = Screen::Select;
            show_track(race_track_id());
            break;
        case CourseAction::Back: screen_ = Screen::Title; break;
        case CourseAction::None: break;
        }
        return;
    case Screen::Select:
        switch (update_select(select_, act, parts_)) {
        case SelectAction::Accept:
            options_.build = select_.build;
            start_race();
            screen_ = Screen::Race;
            break;
        case SelectAction::Back: screen_ = Screen::Course; break;
        default: break;
        }
        return;
    case Screen::Race: {
        // Esc leaves the race, R restarts it, and after the finish Enter / A
        // shows the results.
        const bool finished = world_ && world_->karts()[size_t(player_)].finished;
        if (update_pause(act, running)) return;
        if (finished && act.accept) {
            finish_offline_race();
            screen_ = Screen::Standings;
        } else if (act.restart_pressed) {
            start_race();
        }
        return;
    }
    case Screen::Standings:
        if (!act.accept && !act.back) return;
        if (course_.mode == GameMode::GrandPrix && !gp_.over && act.accept) {
            ++gp_.race;
            start_race();
            screen_ = Screen::Race;
            return;
        }
        screen_ = course_.mode == GameMode::GrandPrix ? Screen::Title : Screen::Course;
        options_.track.clear();
        cam_init_ = false;
        show_track(course_track(course_, tracks_));
        return;
    }
}

void App::render_menu(float dt) {
    // The chosen kart on the start line, with the camera circling it.
    menu_angle_ += dt * 0.4f;
    if (online_ && online_->has_lobby()) show_track(online_->lobby().track);
    sim::KartState k;
    sim::kart_spawn(k, *shown_track_, 0);
    const vec3 fwd = k.forward();
    const vec3 orbit = glm::angleAxis(menu_angle_, kUp) * (fwd * 4.2f);
    render::Camera cam;
    cam.position = k.pos + orbit + kUp * 1.6f;
    if (!options_.preview_model.empty()) { // close and high, to inspect a model
        cam.position = k.pos + orbit * 0.32f + kUp * 1.3f;
    }
    // Look past the kart so it sits right of centre, clear of the menu panel.
    cam.target = k.pos + kUp * 0.5f - glm::normalize(glm::cross(kUp, orbit)) * 1.3f;
    if (!options_.preview_model.empty()) cam.target = k.pos + kUp * 0.25f;
    cam.fov_y_deg = 55.0f;
    if (!renderer_.begin_frame(cam, light_)) return;
    renderer_.draw(track_mesh_, mat4(1.0f));
    renderer_.draw(scenery_mesh_, mat4(1.0f));
    if (prop_meshes_)
        for (render::MeshId id : *prop_meshes_) renderer_.draw(id, mat4(1.0f));
    const int mesh = online_ ? std::max(online_->seat(), 0) : std::clamp(options_.karts, 1, 12) - 1;
    const mat4 kart_m = glm::translate(mat4(1.0f), k.pos) * glm::mat4_cast(k.rot);
    renderer_.draw(shadow_mesh_, glm::scale(glm::translate(kart_m, vec3(0, 0.03f, -0.02f)), vec3(1.15f, 1.0f, 1.55f)));
    if (const GpuModel* preview = options_.preview_model.empty() ? nullptr : assets_.item(options_.preview_model)) {
        draw_model(*preview, kart_m);
        // The kart's axes, for checking a model's orientation: red +X, green +Y, blue +Z.
        renderer_.draw(spark_mesh_, glm::scale(glm::translate(kart_m, vec3(1.5f, 0.1f, 0)), vec3(2.0f)), vec4(3, 0, 0, 1));
        renderer_.draw(spark_mesh_, glm::scale(glm::translate(kart_m, vec3(0, 1.5f, 0)), vec3(2.0f)), vec4(0, 3, 0, 1));
        renderer_.draw(spark_mesh_, glm::scale(glm::translate(kart_m, vec3(0, 0.1f, 1.5f)), vec3(2.0f)), vec4(0, 0, 3, 1));
    } else {
        draw_kart_visual(build_visual(select_.build, mesh), kart_m, vec4(1.0f), 0.0f);
    }
    if (options_.hide_ui) {
        renderer_.end_frame();
        return;
    }
    switch (screen_) {
    case Screen::Title: draw_title(renderer_, title_); break;
    case Screen::Options: draw_options(renderer_, options_menu_, settings_); break;
    case Screen::Online: draw_online_status(renderer_, online_error_.empty() ? "SIGNING IN AND FINDING A RACE..." : online_error_, !online_error_.empty()); break;
    case Screen::OnlineMenu:
        draw_online_menu(renderer_, online_menu_, !(!options_.master_url.empty() ? options_.master_url : settings_.master_url).empty());
        break;
    case Screen::Join: draw_join(renderer_, join_text_, join_hint_, (frame_ / 30) % 2 == 0); break;
    case Screen::Hosting: draw_hosting(renderer_, host_lines_, host_failed_); break;
    case Screen::Course: draw_course(renderer_, course_, tracks_); break;
    default: {
        const net::LobbyMsg* lobby = online_ && online_->has_lobby() ? &online_->lobby() : nullptr;
        std::string where = shown_track_->name;
        if (lobby && !lobby->cup.empty()) where = lobby->cup + ": " + where;
        else if (!online_ && course_.mode == GameMode::GrandPrix && !tracks_.cups.empty())
            where = tracks_.cups[size_t(course_.cup % int(tracks_.cups.size()))].name + ": " + shown_track_->name;
        draw_select(renderer_, select_, parts_, lobby, where);
        if (online_ && !host_share_.empty()) {
            const std::string t = host_share_;
            renderer_.ui_rect(6.0f, 182.0f, render::Renderer::ui_text_width(t.c_str(), 1.0f) + 12.0f, 16.0f, render::rgba(0, 0, 0, 160));
            renderer_.ui_text(12.0f, 186.0f, t.c_str(), render::rgba(255, 230, 120), 1.0f);
        }
        if (online_ && !account_label_.empty()) {
            const std::string t = "SIGNED IN: " + account_label_;
            renderer_.ui_rect(6.0f, 200.0f, render::Renderer::ui_text_width(t.c_str(), 1.0f) + 12.0f, 16.0f, render::rgba(0, 0, 0, 140));
            renderer_.ui_text(12.0f, 204.0f, t.c_str(), render::rgba(170, 215, 255), 1.0f);
        }
        break;
    }
    }
    if (online_) draw_net_status();
    renderer_.end_frame();
}

void App::draw_online_results() {
    const net::ResultsMsg& res = *online_->results();
    const bool gp_over = res.race_number >= res.races_per_gp;
    std::vector<ResultRow> rows;
    for (const auto& e : res.entries)
        rows.push_back({e.name, int(e.finish_ticks), e.points, e.total, int(e.seat) == online_->seat()});
    // After the last race, rank by Grand Prix total.
    if (gp_over)
        std::stable_sort(rows.begin(), rows.end(), [](const ResultRow& a, const ResultRow& b) { return a.total > b.total; });
    char title[64];
    if (gp_over) std::snprintf(title, sizeof title, "GRAND PRIX RESULTS");
    else std::snprintf(title, sizeof title, "RACE %d OF %d", res.race_number, res.races_per_gp);
    std::string footer;
    if (online_->has_lobby() && online_->lobby().seconds_left > 0)
        footer = "NEXT RACE LOBBY IN " + std::to_string(online_->lobby().seconds_left);
    draw_results(renderer_, title, rows, footer);
}

void App::draw_net_status() {
    const VkExtent2D ext = renderer_.ui_extent();
    const float H = float(ext.height);
    std::string line;
    if (!net_->connected()) line = "DISCONNECTED FROM SERVER";
    else if (!online_->joined()) line = "CONNECTING...";
    else if (screen_ == Screen::Race && (!online_->world() || !online_->has_clock())) line = "WAITING FOR THE SERVER...";
    else if (screen_ == Screen::Race && online_->seat() < 0 && online_->phase() == net::RoomPhase::Racing)
        line = "SPECTATING - YOU JOIN THE NEXT RACE";
    if (!line.empty()) {
        ui::title(renderer_, float(ext.width) * 0.5f, H * 0.35f, line.c_str(), 2.0f, ui::kText);
    }
    // Connection pill: a coloured dot for the ping, and whether we host.
    char buf[96];
    const int ping = net_->rtt_ms();
    std::snprintf(buf, sizeof(buf), "%s%d ms%s", host_process_ ? "HOSTING   " : "", ping, online_->data_mismatch() ? "  DATA MISMATCH" : "");
    const float tw = render::Renderer::ui_text_width(buf, 1.0f), pw = tw + 26.0f;
    const float px = float(ext.width) - pw - 12.0f, py = screen_ == Screen::Race ? 64.0f : H - 88.0f;
    renderer_.ui_round_rect(px, py, pw, 15.0f, 7.5f, render::rgba(10, 14, 40, 170));
    renderer_.ui_round_rect(px + 6.0f, py + 4.0f, 7.0f, 7.0f, 3.5f,
                            ping < 80 ? render::rgba(90, 230, 120) : ping < 160 ? render::rgba(255, 210, 60) : render::rgba(255, 90, 80));
    renderer_.ui_text(px + 18.0f, py + 4.0f, buf, ui::kSubtle, 1.0f);
}

void App::shutdown() {
    stop_hosting();
    if (matching_.valid()) matching_.wait();
    online_.reset();
    net_.reset();
    if (transport_ready_) net::transport_shutdown();
    renderer_.shutdown();
    if (window_) SDL_DestroyWindow(window_);
    window_ = nullptr;
    if (const std::string sp = settings_path(); !sp.empty()) settings_.save(sp);
    audio_.close();
    SDL_Quit();
}

} // namespace gk
