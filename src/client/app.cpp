#include "app.h"
#include "ui_style.h"

#include <filesystem>

#include "render/item_mesh.h"
#include "render/kart_mesh.h"

#include "gk/online/master_client.h"
#include "render/lava.h"
#include "render/track_mesh.h"
#include "render/item_icons.h"
#include "render/sky.h"
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
    // Course packs built from model files on this computer (assets/, not shipped).
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
    // Where the start-up time goes, for the log.
    const uint64_t t_freq = SDL_GetPerformanceFrequency();
    uint64_t t_last = SDL_GetPerformanceCounter();
    auto lap = [&] {
        const uint64_t now = SDL_GetPerformanceCounter();
        const double ms = double(now - t_last) * 1000.0 / double(t_freq);
        t_last = now;
        return int(ms);
    };
    render::RendererOptions ro;
    ro.full_resolution = options_.full_resolution;
    ro.validation = options_.validation;
    if (!renderer_.init(window_, ro)) return false;
    const int ms_renderer = lap();
    renderer_.make_particle_texture();
    if (!renderer_.load_font(sim::data_path("fonts/RussoOne-Regular.ttf"))) log_warn("UI font missing: using the pixel font");
    else renderer_.set_ui_icons(render::paint_item_icons(512), 512, render::kIconGrid);
    const int ms_ui = lap();
    // The game's own painted items, then an optional model pack beside the
    // data folder (not in the repository) that may override them.
    // Low-poly: half the segments of the smooth shapes; the painted textures
    // carry the detail.
    render::MeshBuilder::detail = 0.5f;
    for (const auto& [name, model] : render::make_gobby_items()) assets_.add_builtin_item(name, model, renderer_);
    {
        std::vector<render::PaintedModel> tires(parts_.tires.size());
        render::parallel_for(int(tires.size()), [&](int i) { tires[size_t(i)] = render::make_gobby_tire(parts_.tires[size_t(i)].look); });
        for (size_t i = 0; i < tires.size(); ++i) assets_.add_builtin_tire(parts_.tires[i].id, tires[i], renderer_);
    }
    assets_.add_builtin_driver("meowie", render::make_meowie(), renderer_);
    const int ms_items = lap();
    // Map props: kept on the CPU to merge per track, with one texture each.
    for (auto& [name, model] : render::make_gobby_props()) {
        const render::TextureId tex = renderer_.upload_texture(model.texture.size(), model.texture.size(), model.texture.rgba().data());
        props_.emplace(name, PropModel{std::move(model.mesh), tex, model.two_sided, model.blended, model.bounds_min, model.bounds_max, model.glow, model.unlit});
    }
    const int ms_props = lap();
    if (assets_.load(sim::data_path("../assets/models"), renderer_)) log_info("Model pack loaded from assets/models");
    log_info("Start-up: renderer {} ms, ui {} ms, items {} ms, props {} ms, pack {} ms", ms_renderer, ms_ui, ms_items, ms_props, lap());
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
    if (options_.pixel_look) settings_.low_res = true;
    if (options_.volumetric) settings_.volumetric = true;
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
        if (world_ && options_.give_item > 0 && options_.give_item < int(sim::Item::Count)) world_->give_item(player_, sim::Item(options_.give_item));
        if (world_ && options_.spawn_obstacle >= 0 && size_t(options_.spawn_obstacle) < world_->track().obstacles.size()) {
            const auto& o = world_->track().obstacles[size_t(options_.spawn_obstacle)];
            sim::KartState& k = world_->karts_mut()[size_t(player_)];
            k.pos = o.pos + o.up * 0.3f - o.forward * 14.0f;
            k.rot = glm::normalize(glm::quat_cast(glm::mat3(-o.right, o.up, o.forward)));
            k.sample = world_->track().nearest_sample(k.pos);
            prev_ = world_->karts();
        }
        if (world_ && options_.spawn_sample >= 0 && size_t(options_.spawn_sample) < world_->track().road.size()) {
            const sim::CenterSample& c = world_->track().sample(options_.spawn_sample);
            sim::KartState& k = world_->karts_mut()[size_t(player_)];
            k.pos = c.pos + c.up * 0.1f;
            k.rot = glm::normalize(glm::quat_cast(glm::mat3(-c.right, c.up, c.tangent)));
            k.sample = k.last_safe_sample = options_.spawn_sample;
            prev_ = world_->karts();
        }
        if (world_ && options_.start_lap > 1)
            for (sim::KartState& k : world_->karts_mut()) k.lap = options_.start_lap;
    } else {
        if (options_.menu == "options") screen_ = Screen::Options;
        else if (options_.menu == "controls") screen_ = Screen::Controls;
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
    fireball_mesh_ = renderer_.upload_mesh(render::make_fireball_mesh());
    oil_disc_mesh_ = renderer_.upload_mesh(render::make_oil_disc_mesh());
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
    log_info("Renderer ready, internal resolution {}x{} ({} ms for the first track and karts)", renderer_.internal_extent().width,
             renderer_.internal_extent().height, lap());
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
    v.gobby = gobby_kart;
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
    {   // Paint the standard kart in every colour not painted yet, on all cores at once.
        std::vector<uint32_t> paints;
        for (int i = 0; i < 12; ++i)
            if (const uint32_t c = kart_color(i); !gobby_karts_.count(c) && std::find(paints.begin(), paints.end(), c) == paints.end()) paints.push_back(c);
        std::vector<render::PaintedModel> painted(paints.size());
        render::parallel_for(int(paints.size()), [&](int i) { painted[size_t(i)] = render::make_gobby_kart(paints[size_t(i)]); });
        for (size_t i = 0; i < paints.size(); ++i) gobby_karts_.emplace(paints[i], AssetLibrary::upload_painted(painted[i], renderer_));
    }
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
        const uint64_t t0 = SDL_GetPerformanceCounter();
        auto ms = [&] { return int(double(SDL_GetPerformanceCounter() - t0) * 1000.0 / double(SDL_GetPerformanceFrequency())); };
        const render::MeshId track = renderer_.upload_mesh(render::make_track_mesh(*shown_track_, true));
        if (const render::MeshData oil = render::make_oil_mesh(*shown_track_); !oil.indices.empty()) oil_meshes_[shown_track_->id] = renderer_.upload_mesh(oil);
        const int ms_road = ms();
        std::vector<render::PropPlacement> placements;
        const render::MeshId scenery = renderer_.upload_mesh(render::make_scenery_mesh(*shown_track_, &placements));
        const int ms_scenery = ms();
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
        {   // Point lights where the glowing props stand.
            std::vector<render::PointLight>& lights = track_lights_[shown_track_->id];
            const bool night = shown_track_->theme == "night", dark = night || shown_track_->theme == "volcano";
            for (const auto& p : placements) {
                render::PointLight l;
                if (p.name == "lamp_post") l.pos = vec3(p.transform * vec4(1.0f, 5.2f, 0.0f, 1.0f)), l.color = vec3(1.0f, 0.86f, 0.58f), l.radius = 20.0f, l.intensity = night ? 2.2f : 0.35f;
                else if (p.name == "neon_pink") l.pos = vec3(p.transform * vec4(0.0f, 1.6f, 0.6f, 1.0f)), l.color = vec3(1.0f, 0.35f, 0.8f), l.radius = 14.0f, l.intensity = 1.1f;
                else if (p.name == "neon_cyan") l.pos = vec3(p.transform * vec4(0.0f, 1.6f, 0.6f, 1.0f)), l.color = vec3(0.3f, 0.9f, 1.0f), l.radius = 14.0f, l.intensity = 1.1f;
                else if (p.name == "lantern") l.pos = vec3(p.transform * vec4(0.0f, 0.0f, 0.0f, 1.0f)), l.color = vec3(1.0f, 0.72f, 0.38f), l.radius = 8.0f, l.intensity = 0.8f;
                else if (p.name == "lava_rock") l.pos = vec3(p.transform * vec4(0.0f, 0.6f, 0.0f, 1.0f)), l.color = vec3(1.0f, 0.42f, 0.1f), l.radius = 11.0f, l.intensity = dark ? 0.9f : 0.5f;
                else continue;
                lights.push_back(l);
            }
            log_info("Track {}: {} point lights", shown_track_->id, lights.size());
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
        {   // Roads that vanish, barriers, switches and zones: drawn when they're there.
            const sim::Track& t = *shown_track_;
            GateMeshes& g = gate_meshes_[t.id];
            for (size_t b = 0; b < t.branches.size(); ++b) {
                if (!t.branches[b].vanish) continue;
                const render::MeshId road = renderer_.upload_mesh(render::make_track_mesh(t, true, int(b) + 1));
                const render::PaintedModel side = render::make_trackside(t, int(b) + 1);
                const render::TextureId tex = renderer_.upload_texture(side.texture.size(), side.texture.size(), side.texture.rgba().data());
                g.roads.push_back({int(b), {road, renderer_.upload_mesh(side.mesh, tex, false)}});
            }
            for (const auto& bar : t.barriers) g.barriers.push_back(renderer_.upload_mesh(render::make_barrier_mesh(bar)));
            // Lava zones draw with the lava layer (mesh.frag), lit from within.
            for (const auto& z : t.zones) g.zones.push_back(renderer_.upload_mesh(render::make_zone_mesh(t, z), 0, z.deep)); // deep water: seen from under it too
            lava_spots_[t.id] = render::find_lava(t);
            for (const auto& sw : t.switches)
                g.switches.push_back({renderer_.upload_mesh(render::make_switch_mesh(sw.radius, false)),
                                      renderer_.upload_mesh(render::make_switch_mesh(sw.radius, true), 0, false, true)});
        }
        log_info("Track {}: {} props in {} meshes; {} ms (road {}, scenery {}, props and trackside {})", shown_track_->id, placements.size(), ids.size(),
                 ms(), ms_road, ms_scenery - ms_road, ms() - ms_scenery);
        it = track_meshes_.emplace(shown_track_->id, std::pair(track, scenery)).first;
    }
    track_mesh_ = it->second.first;
    scenery_mesh_ = it->second.second;
    prop_meshes_ = &track_props_[shown_track_->id];
    lights_ = &track_lights_[shown_track_->id];
    oil_mesh_ = oil_meshes_.count(shown_track_->id) ? oil_meshes_[shown_track_->id] : 0;
    const render::ThemeStyle& th = render::theme_style(shown_track_->theme);
    light_ = {};
    light_.sky = th.sky;
    {   // The painted sky for the theme, made once.
        auto sky = sky_textures_.find(shown_track_->theme);
        if (sky == sky_textures_.end()) {
            const render::SkyImage img = render::paint_sky(shown_track_->theme);
            sky = sky_textures_.emplace(shown_track_->theme, std::pair(renderer_.upload_texture(img.width, img.height, img.rgba.data()),
                                                                       std::pair(img.zenith, img.horizon))).first;
        }
        light_.sky_texture = sky->second.first;
        light_.zenith = sky->second.second.first;
        light_.sky = sky->second.second.second; // distant things fade into the horizon
    }
    light_.fog_density = th.fog_density;
    light_.ambient = th.ambient;
    light_.lights = *lights_;
    lava_ = &lava_spots_[shown_track_->id];
    lava_phase_.clear();
    lava_light_base_ = light_.lights.size();
    for (const auto& l : lava_->lights) light_.lights.push_back({l.pos, 18.0f, vec3(1.0f, 0.42f, 0.1f), 1.1f});
    renderer_.set_heat(lava_->pool, lava_->pool_y);
    if (lava_->pool || !lava_->zone_lines.empty())
        log_info("Track {}: lava in {} pool cells and {} zone samples, {} lights", shown_track_->id, lava_->pool_cells.size(), lava_->zone_lines.size(),
                 lava_->lights.size());
    if (shown_track_->theme == "rainbow") { // a low, warm dusk sun
        light_.light_dir = glm::normalize(vec3(0.75f, 0.32f, 0.35f));
        light_.ambient = 0.6f;
    }
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
    particles_.clear();
    skids_.clear();
    skid_last_.clear();
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
    if (!options_.demo_fx.empty() && world_ && frame_ == 250 && size_t(player_) < world_->karts().size()) {
        sim::KartState& k = world_->karts_mut()[size_t(player_)];
        if (options_.demo_fx == "explosion") world_->spawn_explosion(k.pos + k.forward() * 5.0f, player_);
        else if (options_.demo_fx == "hit") world_->apply_hit(player_, sim::HitKind::Spin, -1);
        else if (options_.demo_fx == "tumble") world_->apply_hit(player_, sim::HitKind::Tumble, -1);
        else if (options_.demo_fx == "trick") k.trick_anim = 1.0f, event_particles({sim::KartEventType::Trick, player_, 0});
        else if (options_.demo_fx == "oil") // every other kart pours out a slick, so the player sees them ahead
            for (size_t i = 0; i < world_->karts().size(); ++i)
                if (int(i) != player_) world_->throw_item(int(i), sim::Item::OilSlick, true, false);
    }
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
        event_particles(e);
        if (e.kart != player_) continue;
        switch (e.type) {
        case sim::KartEventType::Boost:
            input_.rumble(0.3f, 0.6f, 150);
            break;
        case sim::KartEventType::Trick: input_.rumble(0.2f, 0.2f, 80); break;
        case sim::KartEventType::WaterExit: wet_lens_ = 1.0f; break;
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
    renderer_.set_volumetric(settings_.volumetric);
    show_help_ = settings_.show_help;
    input_.rumble_enabled = settings_.rumble;
    input_.binds = settings_.binds;
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
        case sim::Item::TripleBanana:
        case sim::Item::OilSlick: s = Sfx::Drop; break;
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

void App::spring(float& x, float& v, float target, float omega, float dt) {
    // Critically damped: no overshoot, smooth in and out.
    dt = std::min(dt, 0.05f);
    const float f = 1.0f + omega * dt;
    const float oo = omega * omega;
    const float det = 1.0f / (f * f + dt * dt * oo);
    const float nx = (f * x + dt * v + dt * dt * oo * target) * det;
    v = (v + dt * oo * (target - x)) * det;
    x = nx;
}

void App::draw_kart(int index, const sim::KartState& s, float alpha) {
    const sim::KartState& p = prev_.size() > size_t(index) ? prev_[size_t(index)] : s;
    vec3 pos = glm::mix(p.pos, s.pos, alpha);
    if (online_) pos += online_->visual_offset(index);
    const quat rot = glm::slerp(p.rot, s.rot, alpha);
    // Bumping down the stairs: a hop on every step.
    if (s.grounded && world() && world()->track().sample(s.sample).branch < 0 && world()->track().on_stairs(world()->track().sample(s.sample).s)) {
        const sim::CenterSample& c = world()->track().sample(s.sample);
        const float along = glm::dot(pos - c.pos, c.tangent) + c.s;
        pos.y += 0.09f * std::abs(std::sin(along / 1.2f * kPi));
    }
    mat4 m = glm::translate(mat4(1.0f), pos) * glm::mat4_cast(rot);
    // The body's swing: the sim's nose angle and the drift roll, eased by a
    // spring so going into and out of a drift is one smooth motion.
    KartPose& pose = poses_[size_t(index % 12)];
    {
        const float dt = paused_ ? 0.0f : frame_dt_;
        const float slide = glm::dot(s.slide, s.right());
        const float roll_target = (s.drift == sim::DriftState::Drifting ? float(s.drift_dir) * 0.085f : 0.0f) + std::clamp(slide * 0.012f, -0.06f, 0.06f);
        spring(pose.yaw, pose.yaw_v, s.visual_drift_yaw, 14.0f, dt);
        spring(pose.roll, pose.roll_v, s.grounded ? roll_target : 0.0f, 11.0f, dt);
        // A nod when a boost kicks in and when the kart lands.
        const float pitch_target = s.boost_frames > 0 ? -0.025f : 0.0f;
        spring(pose.pitch, pose.pitch_v, pitch_target, 9.0f, dt);
    }
    m = glm::rotate(m, -pose.yaw, vec3(0, 1, 0));
    if (s.gliding) { // bank into the turn, nose up or down with the stick
        const float bank = glm::mix(p.glide_bank, s.glide_bank, alpha), pitch = glm::mix(p.glide_pitch, s.glide_pitch, alpha);
        m = glm::translate(m, vec3(0, 0.6f, 0));
        m = glm::rotate(m, bank, vec3(0, 0, 1));
        m = glm::rotate(m, -pitch, vec3(1, 0, 0));
        m = glm::translate(m, vec3(0, -0.6f, 0));
    }
    auto ease_out = [](float t) { t = std::clamp(t, 0.0f, 1.0f); return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t); };
    auto smooth = [](float t) { t = std::clamp(t, 0.0f, 1.0f); return t * t * (3.0f - 2.0f * t); };
    if (s.trick_anim > 0.0f) { // a barrel roll that whips round and settles, with a little hop
        const float t = 1.0f - s.trick_anim;
        m = glm::translate(m, vec3(0, std::sin(t * kPi) * 0.25f, 0));
        m = glm::rotate(m, smooth(t) * 2.0f * kPi, vec3(0, 0, 1));
    }
    m = glm::rotate(m, pose.roll, vec3(0, 0, 1));
    m = glm::rotate(m, pose.pitch, vec3(1, 0, 0));
    if (s.wall_bump > 0.0f) m = glm::rotate(m, std::sin(float(frame_) * 1.7f) * 0.08f * s.wall_bump, vec3(0, 1, 0));
    const mat4 base = m; // before hit animations, for held items
    if (s.spin_frames > 0) { // spun out: two fast turns that wind down, bouncing on the wheels
        const float t = 1.0f - float(s.spin_frames) / float(std::max(1, world()->item_tuning().spin_frames));
        const float e = ease_out(t);
        const float bounce = std::abs(std::sin(t * kPi * 3.0f)) * (1.0f - t) * 0.22f;
        m = glm::translate(m, vec3(0, bounce, 0));
        m = glm::rotate(m, e * 4.0f * kPi, vec3(0, 1, 0));
        const float squash = (1.0f - t) * 0.12f * std::max(0.0f, std::cos(t * kPi * 3.0f));
        m = glm::scale(m, vec3(1.0f + squash, 1.0f - squash * 1.4f, 1.0f + squash));
    }
    if (s.tumble_frames > 0) { // blown up: thrown high, flipping over, landing with a thump
        const float t = 1.0f - float(s.tumble_frames) / float(std::max(1, world()->item_tuning().tumble_frames));
        m = glm::translate(m, vec3(0, std::sin(t * kPi) * 1.6f, 0));
        m = glm::rotate(m, smooth(t) * 2.0f * kPi, vec3(1, 0, 0));
        m = glm::rotate(m, std::sin(t * kPi) * 0.6f, vec3(0, 1, 0));
    }
    if (s.shrink_frames > 0) m = glm::scale(m, vec3(0.55f));
    vec4 tint(1.0f);
    if (s.respawn_frames > 0 || (s.invincible_frames > 0 && (frame_ / 4) % 2)) tint = vec4(1, 1, 1, 1) * 0.6f + 0.4f;
    // The glow shader: a white-to-red flash when hit, gold while tricking,
    // rainbow under a Star.
    int fx = 0;
    vec4 glow(0.0f);
    if (s.star_frames > 0) {
        const float h = float(frame_) * 0.3f;
        fx = 3, glow = vec4(0.5f + 0.5f * std::sin(h), 0.5f + 0.5f * std::sin(h + 2.1f), 0.5f + 0.5f * std::sin(h + 4.2f), 0.7f);
    }
    if (s.trick_anim > 0.0f) fx = 3, glow = vec4(1.0f, 0.85f, 0.3f, 0.35f * std::sin((1.0f - s.trick_anim) * kPi));
    if (index < 12 && hit_flash_[size_t(index)] > 0.0f) {
        const float k = hit_flash_[size_t(index)] / 0.45f;
        fx = 3, glow = vec4(glm::mix(vec3(1.0f, 0.25f, 0.2f), vec3(1.0f), k * k), k);
    }
    if (s.ghost_frames > 0) tint = vec4(index == player_ ? 0.45f : 0.12f), fx = 0;
    if (fx == 3) tint = glow;
    const KartVisual& visual = kart_visuals_[size_t(index % 12)];
    if (s.grounded && s.respawn_frames == 0)
        renderer_.draw(shadow_mesh_, glm::scale(glm::translate(base, vec3(0, 0.03f, -0.02f)), vec3(1.15f, 1.0f, 1.55f)));
    if (!visual.layout.wheels.empty() && index < 12)
        wheel_angle_[size_t(index)] += s.speed / visual.layout.wheels[0].radius * frame_dt_;
    if (s.bullet_frames > 0) {
        draw_item_mesh(bullet_mesh_, "bullet", base);
        // The Bullet's rocket plume: a big blue-white jet from its tail.
        const float flicker = 0.9f + 0.1f * std::sin(float(frame_) * 2.3f + float(index));
        draw_flame(base, vec3(0, 0.8f, -1.3f), vec3(0, 0, -1), 0.8f, 1.9f * flicker, vec3(0.4f, 0.7f, 1.0f), float(index) * 0.137f);
    } else {
        renderer_.set_fx(fx);
        draw_kart_visual(visual, m, tint, index < 12 ? wheel_angle_[size_t(index)] : 0.0f);
        renderer_.set_fx(0);
    }
    if (s.chomper_frames > 0) {
        const float lunge = s.chomper_cooldown > world()->item_tuning().chomper_cooldown - 10 ? 0.8f : 0.0f;
        draw_item_mesh(chomper_mesh_, "chomper", glm::translate(base, vec3(0, 0.3f, 1.1f + lunge)));
    }
    if (s.item != sim::Item::None && s.roulette == 0 && s.bullet_frames == 0)
        draw_held_item(s.item, s.item_uses, base, s.trailing);
    if (s.gliding) draw_glider_visual(visual, m, tint);

    const auto& kc = world()->tables().constants;
    emit_kart_particles(index, s, m);
    if (s.boost_frames > 0 && s.bullet_frames == 0) {
        // Jets of cartoon flame from the exhausts, in the boost's colour.
        const float flicker = 0.85f + 0.15f * std::sin(float(frame_) * 2.1f + float(index) * 1.7f);
        vec3 c(1.0f, 0.55f, 0.12f);
        if (s.boost == sim::BoostKind::MiniTurbo) c = vec3(to_tint(tier_color(1)));
        if (s.boost == sim::BoostKind::SuperMiniTurbo) c = vec3(to_tint(tier_color(2)));
        if (s.boost == sim::BoostKind::UltraMiniTurbo) c = vec3(to_tint(tier_color(3)));
        if (s.boost == sim::BoostKind::Slipstream) c = vec3(0.45f, 0.8f, 1.0f);
        const float strength = std::min(1.0f, float(s.boost_frames) / 20.0f); // dies down at the end
        const float phase = float(index) * 0.137f;
        if (visual.gobby) {
            for (float side : {-1.0f, 1.0f}) {
                const vec3 d = glm::normalize(vec3(side * 0.22f, 0.2f, -1.0f));
                const vec3 pipe = vec3(side * 0.26f, 0.7f, -1.02f) + glm::normalize(vec3(side * 0.25f, 0.55f, -1.0f)) * 0.3f;
                draw_flame(m, pipe, d, 0.32f, (0.9f + 0.5f * strength) * flicker, c, phase + (side > 0 ? 0.3f : 0.0f));
            }
        } else {
            draw_flame(m, vec3(0, 0.3f, -0.95f), vec3(0, 0, -1), 0.42f, (1.1f + 0.5f * strength) * flicker, c, phase);
        }
    }
}

void App::draw_flame(const mat4& m, vec3 pos, vec3 dir, float width, float length, vec3 color, float phase) {
    // The flame mesh points down -Z: build a frame whose -Z is `dir`.
    const vec3 f = -glm::normalize(dir);
    const vec3 ref = std::abs(f.y) < 0.9f ? vec3(0, 1, 0) : vec3(1, 0, 0);
    const vec3 r = glm::normalize(glm::cross(ref, f));
    const vec3 u = glm::cross(f, r);
    const mat4 frame(vec4(r, 0.0f), vec4(u, 0.0f), vec4(f, 0.0f), vec4(pos, 1.0f));
    renderer_.set_fx(4);
    renderer_.draw(flame_mesh_, m * frame * glm::scale(mat4(1.0f), vec3(width, width, length)), vec4(color, std::fmod(phase, 0.98f)));
    renderer_.set_fx(0);
}

void App::emit_kart_particles(int index, const sim::KartState& s, const mat4& m) {
    using Sp = render::Renderer::Sprite;
    const float dt = frame_dt_;
    if (dt <= 0.0f || paused_) return;
    const auto& kc = world()->tables().constants;
    auto at = [&](vec3 local) { return vec3(m * vec4(local, 1.0f)); };
    auto dir = [&](vec3 local) { return glm::normalize(vec3(m * vec4(local, 0.0f))); };
    const vec3 vel = s.velocity();
    const float speed = glm::length(vel);
    auto count = [&](float rate) {
        const float n = rate * dt;
        return int(n) + (particles_.rnd() < n - std::floor(n) ? 1 : 0);
    };
    auto R = [&] { return particles_.rnd(); };
    const bool near = glm::distance(s.pos, cam_pos_) < 70.0f;

    // Skid marks: rubber strips behind each rear tyre, as wide as the tyre,
    // fading in as the slide starts and out as it ends.
    {
        const bool sliding_hard = s.grounded && speed > 5.0f && (s.drift == sim::DriftState::Drifting || glm::length(s.slide) > 4.0f);
        const size_t slot = size_t(index % 12);
        if (skid_last_.size() < 12) skid_last_.assign(12, SkidPoint{});
        SkidPoint& last = skid_last_[slot];
        const float target = !sliding_hard ? 0.0f : s.drift == sim::DriftState::Drifting ? 0.55f + 0.45f * std::min(1.0f, glm::length(s.slide) / 8.0f) : 0.7f;
        last.strength += (target - last.strength) * std::min(1.0f, dt * (target > last.strength ? 5.0f : 3.0f));
        // Rear wheels from the kart's layout: their spots and widths.
        const KartVisual& kv = kart_visuals_[slot];
        SkidPoint now;
        now.strength = last.strength;
        for (const auto& wl : kv.layout.wheels)
            if (wl.center.z < 0.0f && now.wheels < 2) {
                now.wheel[now.wheels] = at({wl.center.x, 0.0f, wl.center.z}) + s.up() * 0.035f;
                now.width[now.wheels] = std::clamp(wl.radius * 0.75f, 0.12f, 0.4f);
                ++now.wheels;
            }
        if (now.wheels == 0) {
            now.wheels = 2;
            for (int w = 0; w < 2; ++w) now.wheel[w] = at({w == 0 ? -0.62f : 0.62f, 0.0f, -0.6f}) + s.up() * 0.035f, now.width[w] = 0.26f;
        }
        now.across = glm::normalize(vec3(m * vec4(1, 0, 0, 0)));
        if (last.strength < 0.03f) {
            last.valid = false;
        } else if (!last.valid || last.wheels != now.wheels) {
            now.valid = true;
            now.v = 0.0f;
            last = now;
        } else if (glm::distance(now.wheel[0], last.wheel[0]) > 0.3f) {
            now.valid = true;
            now.v = last.v + glm::distance(now.wheel[0], last.wheel[0]) / 1.2f;
            for (int w = 0; w < now.wheels; ++w) {
                Skid k;
                const float h0 = 0.5f * last.width[w], h1 = 0.5f * now.width[w];
                k.p[0] = last.wheel[w] - last.across * h0, k.p[1] = last.wheel[w] + last.across * h0;
                k.p[2] = now.wheel[w] + now.across * h1, k.p[3] = now.wheel[w] - now.across * h1;
                k.v0 = std::fmod(last.v, 1.0f), k.v1 = k.v0 + (now.v - last.v);
                k.a0 = last.strength, k.a1 = now.strength;
                k.age = 0.0f;
                skids_.push_back(k);
            }
            if (skids_.size() > 2400) skids_.erase(skids_.begin(), skids_.begin() + 200);
            last = now;
        }
    }

    // Drift sparks: a spiky star at each rear wheel in the charge colour.
    if (s.drift == sim::DriftState::Drifting && s.mt_charge >= kc.spark_threshold) {
        const vec3 c = vec3(to_tint(tier_color(s.mt_tier(kc))));
        for (float side : {-1.0f, 1.0f}) {
            const vec3 wheel = at({side * 0.68f, 0.14f, -0.95f});
            // One spiky star stuck to the wheel, swapping shape, angle and size
            // a dozen times a second: a flickering cartoon spark.
            const int swap = int(double(SDL_GetTicks()) / 1000.0 * 14.0) + index * 3 + (side > 0.0f ? 1 : 0);
            const uint32_t h = uint32_t(swap) * 2654435761u;
            const Sp shapes[3] = {Sp::Spark6, Sp::Spark8, Sp::Burst};
            const float size = 0.26f + 0.08f * float((h >> 16) % 100) / 100.0f + 0.04f * float(s.mt_tier(kc));
            renderer_.particle(wheel + s.up() * 0.12f, size, vec4(glm::mix(c, vec3(1.0f), 0.15f), 1.0f), false, vec3(0.0f), 0.0f,
                               shapes[h % 3], float((h >> 8) % 628) / 100.0f);
        }
    }
    // Tyre smoke: round white cartoon puffs that pop out and shrink away.
    const bool sliding = s.grounded && (s.drift == sim::DriftState::Drifting || glm::length(s.slide) > 3.0f) && speed > 5.0f;
    if (sliding && near)
        for (int i = 0, n = count(10.0f); i < n; ++i) {
            Particle p;
            const float side = R() < 0.5f ? -1.0f : 1.0f;
            p.pos = at({side * 0.7f, 0.2f, -0.85f});
            p.vel = vel * 0.25f + vec3(0, 1.0f + R(), 0) + particles_.rnd_dir() * 0.4f;
            p.life = 0.45f + 0.25f * R();
            p.size0 = 0.12f + 0.06f * R(), p.size1 = 0.2f;
            p.sprite = R() < 0.5f ? Sp::Puff : Sp::Puff2;
            p.pop = 0.2f;
            p.angle = R() * 6.28f, p.spin = (R() - 0.5f) * 3.0f;
            p.color0 = vec4(1.0f, 1.0f, 1.0f, 1.0f), p.color1 = vec4(0.88f, 0.88f, 0.9f, 1.0f);
            p.drag = 2.0f;
            particles_.emit(p);
        }
    // Underwater: a propeller spinning at the back and a stream of bubbles.
    if (s.underwater && near) {
        const float spin = float(double(SDL_GetTicks()) / 1000.0 * 40.0) + float(index);
        renderer_.particle(at({0.0f, 0.45f, -1.15f}), 0.42f, vec4(0.85f, 0.9f, 0.95f, 1.0f), false, vec3(0.0f), 0.0f, Sp::Spark6, spin);
        renderer_.particle(at({0.0f, 0.45f, -1.12f}), 0.1f, vec4(0.35f, 0.35f, 0.4f, 1.0f), false, vec3(0.0f), 0.0f, Sp::Dot);
        for (int i = 0, n = count(10.0f + speed * 0.6f); i < n; ++i) {
            Particle p;
            p.pos = at({(R() - 0.5f) * 0.4f, 0.4f + 0.2f * R(), -1.2f});
            p.vel = vel * 0.2f + vec3((R() - 0.5f) * 0.6f, 1.2f + 0.8f * R(), (R() - 0.5f) * 0.6f);
            p.life = 0.9f + 0.6f * R();
            p.size0 = 0.06f + 0.05f * R(), p.size1 = 0.14f + 0.06f * R();
            p.sprite = R() < 0.6f ? Sp::Ring : Sp::Dot;
            p.color0 = vec4(0.85f, 0.97f, 1.0f, 0.85f), p.color1 = vec4(0.85f, 0.97f, 1.0f, 0.0f);
            p.drag = 1.5f;
            particles_.emit(p);
        }
    }
    // Boost: licks of flame from the exhaust, yellow at the root to red at the tip.
    if (s.boost_frames > 0) {
        vec3 hot(1.0f, 0.92f, 0.35f), cool(1.0f, 0.3f, 0.08f);
        auto tier = [&](int t) { const vec3 c = vec3(to_tint(tier_color(t))); hot = glm::mix(c, vec3(1.0f), 0.6f), cool = c; };
        if (s.boost == sim::BoostKind::MiniTurbo) tier(1);
        if (s.boost == sim::BoostKind::SuperMiniTurbo) tier(2);
        if (s.boost == sim::BoostKind::UltraMiniTurbo) tier(3);
        if (s.boost == sim::BoostKind::Slipstream) hot = vec3(0.9f, 1.0f, 1.0f), cool = vec3(0.4f, 0.8f, 1.0f);
        // Embers that break off the jets and trail behind.
        for (int i = 0, n = count(near ? 16.0f : 5.0f); i < n; ++i) {
            Particle p;
            p.pos = at({(R() - 0.5f) * 0.5f, 0.45f + 0.3f * R(), -1.3f});
            p.carry = vel;
            p.vel = vel + dir({(R() - 0.5f) * 0.6f, 0.4f + 0.4f * R(), -1.0f}) * (3.0f + 2.0f * R());
            p.life = 0.18f + 0.12f * R();
            p.size0 = 0.1f, p.size1 = 0.03f;
            p.sprite = Sp::Dot;
            p.additive = true;
            p.color0 = vec4(hot, 1.0f), p.color1 = vec4(cool, 0.0f);
            p.drag = 1.0f;
            particles_.emit(p);
        }
    }
    // Spun out: a dizzy halo of arcs circling the head.
    if (s.spin_frames > 0 || s.tumble_frames > 0) {
        const float t = float(double(SDL_GetTicks()) / 1000.0) * 7.0f + float(index);
        for (int i = 0; i < 3; ++i) {
            const float a = t + float(i) * 2.094f;
            renderer_.particle(at({0, 1.35f, -0.15f}) + vec3(std::cos(a), 0.0f, std::sin(a)) * 0.55f, 0.24f, vec4(1.0f, 1.0f, 1.0f, 0.95f), false,
                               vec3(0.0f), 0.0f, Sp::Swoosh, a + 1.57f);
        }
    }
    // The Bullet: smoke rings and wind lines streaming off the rocket's tail.
    if (s.bullet_frames > 0 && near) {
        for (int i = 0, n = count(18.0f); i < n; ++i) {
            Particle p;
            p.pos = at({(R() - 0.5f) * 0.3f, 0.8f + (R() - 0.5f) * 0.3f, -2.2f - R() * 1.5f});
            p.carry = vel;
            p.vel = vel * 0.15f + dir({0, 0, -1}) * 2.0f;
            p.life = 0.45f + 0.25f * R();
            p.size0 = 0.3f, p.size1 = 1.1f;
            p.sprite = R() < 0.4f ? Sp::Ring : Sp::Puff2;
            p.pop = 0.1f;
            p.spin = (R() - 0.5f) * 3.0f;
            p.color0 = vec4(0.85f, 0.92f, 1.0f, 0.9f), p.color1 = vec4(1.0f, 1.0f, 1.0f, 0.0f);
            p.drag = 2.0f;
            particles_.emit(p);
        }
        for (int i = 0, n = count(30.0f); i < n; ++i) {
            Particle p;
            const float a = R() * 6.28f;
            p.pos = at({std::cos(a) * 0.8f, 0.8f + std::sin(a) * 0.8f, 1.2f});
            p.carry = vel;
            p.vel = vel - dir({0, 0, 1}) * 14.0f;
            p.life = 0.2f;
            p.size0 = p.size1 = 0.04f;
            p.sprite = Sp::Streak, p.stretch = 0.08f;
            p.additive = true;
            p.color0 = vec4(0.8f, 0.95f, 1.0f, 0.7f), p.color1 = vec4(0.6f, 0.9f, 1.0f, 0.0f);
            particles_.emit(p);
        }
    }
    // Dust off rough ground: tan cartoon puffs.
    using S = sim::Surface;
    const bool rough = s.surface == S::OffroadLight || s.surface == S::OffroadMedium || s.surface == S::OffroadHeavy || s.surface == S::Sand ||
                       s.surface == S::Ice;
    if (s.grounded && rough && speed > 4.0f && near) {
        const std::string& theme = world()->track().theme;
        vec3 dust = theme == "desert" || theme == "canyon" ? vec3(0.93f, 0.74f, 0.5f)
                  : theme == "snow"                      ? vec3(1.0f, 1.0f, 1.0f)
                  : theme == "volcano"                   ? vec3(0.45f, 0.4f, 0.4f)
                                                         : vec3(0.72f, 0.6f, 0.42f);
        if (s.surface == S::Sand) dust = vec3(0.98f, 0.88f, 0.62f);
        for (int i = 0, n = count(6.0f + speed * 0.6f); i < n; ++i) {
            Particle p;
            const float side = R() < 0.5f ? -1.0f : 1.0f;
            p.pos = at({side * 0.7f, 0.15f, -0.8f});
            p.vel = vel * 0.2f + vec3(0, 1.5f + 1.5f * R(), 0) + particles_.rnd_dir() * 0.8f;
            p.life = 0.5f + 0.3f * R();
            p.size0 = 0.14f + 0.07f * R(), p.size1 = 0.22f;
            p.sprite = R() < 0.5f ? Sp::Puff : Sp::Puff2;
            p.pop = 0.25f;
            p.color0 = p.color1 = vec4(dust, 1.0f);
            p.gravity = 2.0f, p.drag = 1.5f;
            particles_.emit(p);
            if (R() < 0.25f) { // a chunk of dirt kicked up
                Particle r = p;
                r.sprite = Sp::Rock, r.pop = 0.1f;
                r.size0 = r.size1 = 0.04f + 0.03f * R();
                r.vel = vel * 0.3f + vec3(0, 3.0f + 2.0f * R(), 0) + particles_.rnd_dir() * 1.5f;
                r.color0 = r.color1 = vec4(dust * 0.75f, 1.0f);
                r.gravity = 18.0f, r.drag = 0.2f, r.spin = (R() - 0.5f) * 12.0f;
                particles_.emit(r);
            }
        }
    }
    // Slipstream: a few long, faint wind lines sliding back along the
    // kart's sides and over the top, like air parting round it.
    if (s.draft > 15 && near)
        for (int i = 0, n = count(14.0f * float(s.draft) / 75.0f); i < n; ++i) {
            Particle p;
            const float side = R() < 0.5f ? -1.0f : 1.0f;
            const bool top = R() < 0.3f;
            p.pos = at({top ? (R() - 0.5f) * 0.6f : side * (0.85f + 0.25f * R()), top ? 1.15f + 0.2f * R() : 0.3f + 0.6f * R(), 0.9f + 0.6f * R()});
            p.carry = vel;
            p.vel = vel - dir({0, 0, 1}) * 9.0f;
            p.life = 0.22f;
            p.size0 = p.size1 = 0.035f;
            p.sprite = Sp::Streak, p.stretch = 0.09f;
            p.color0 = vec4(1.0f, 1.0f, 1.0f, 0.55f), p.color1 = vec4(0.8f, 0.92f, 1.0f, 0.0f);
            particles_.emit(p);
        }
    (void)index;
}

void App::draw_speed_lines(const sim::KartState& k, float top_speed) {
    // Thin tapered streaks rushing in from the screen edges, like an anime
    // speed effect: only at the edges, so the road stays clear. Faint near
    // top speed, strong while boosting.
    const float speed = glm::length(k.velocity());
    float intensity = std::clamp((speed / std::max(top_speed, 1.0f) - 0.97f) / 0.2f, 0.0f, 0.6f);
    if (k.boost_frames > 0) intensity = std::max(intensity, k.boost == sim::BoostKind::BoostPad || k.boost == sim::BoostKind::Mushroom ? 1.0f : 0.8f);
    if (paused_) intensity = 0.0f;
    const float dt = frame_dt_;
    const float n = 90.0f * intensity * dt;
    for (int i = 0, c = int(n) + (particles_.rnd() < n - std::floor(n) ? 1 : 0); i < c; ++i)
        speed_lines_.push_back({particles_.rnd() * 6.2832f, 0.72f + 0.15f * particles_.rnd(), 0.18f + 0.2f * particles_.rnd(),
                                0.14f + 0.08f * particles_.rnd(), 0.0f, 1.2f + 1.8f * particles_.rnd()});
    for (SpeedLine& l : speed_lines_) l.age += dt;
    std::erase_if(speed_lines_, [](const SpeedLine& l) { return l.age >= l.life; });
    const float W = ui::W(renderer_), H = ui::H(renderer_);
    const vec2 c(W * 0.5f, H * 0.52f);
    const float half = 0.5f * std::sqrt(W * W + H * H);
    vec3 tint(1.0f);
    if (k.boost == sim::BoostKind::MiniTurbo || k.boost == sim::BoostKind::SuperMiniTurbo || k.boost == sim::BoostKind::UltraMiniTurbo)
        tint = glm::mix(vec3(1.0f), vec3(to_tint(tier_color(k.boost == sim::BoostKind::MiniTurbo ? 1 : k.boost == sim::BoostKind::SuperMiniTurbo ? 2 : 3))), 0.35f);
    for (const SpeedLine& l : speed_lines_) {
        const float t = l.age / l.life;
        const float r0 = (l.r + 0.45f * t) * half, r1 = r0 - l.len * half; // rushing outward, past the viewer
        const vec2 d(std::cos(l.angle), std::sin(l.angle)), side(-d.y, d.x);
        const float a = std::sin(t * 3.1416f) * 0.55f;
        const uint32_t col = render::rgba(uint8_t(tint.r * 255), uint8_t(tint.g * 255), uint8_t(tint.b * 255), uint8_t(a * 255));
        const vec2 outer = c + d * r0, inner = c + d * r1;
        renderer_.ui_quad(outer - side * l.width, outer + side * l.width, inner + side * 0.15f, inner - side * 0.15f, col);
    }
}

void App::burst(vec3 pos, int n, vec3 color, float speed, float size, render::Renderer::Sprite sprite, float gravity, float life) {
    using Sp = render::Renderer::Sprite;
    for (int i = 0; i < n; ++i) {
        Particle p;
        p.pos = pos;
        vec3 d = particles_.rnd_dir();
        d.y = std::abs(d.y) * 0.8f + 0.2f;
        p.vel = d * speed * (0.5f + 0.5f * particles_.rnd());
        p.life = life * (0.6f + 0.4f * particles_.rnd());
        p.size0 = p.size1 = size * (0.8f + 0.4f * particles_.rnd());
        p.sprite = sprite;
        p.pop = sprite == Sp::Puff ? 0.2f : 0.1f;
        p.angle = particles_.rnd() * 6.28f;
        p.spin = sprite == Sp::Puff ? 1.0f : (particles_.rnd() - 0.5f) * 14.0f;
        p.color0 = p.color1 = vec4(color, 1.0f);
        p.gravity = gravity, p.drag = sprite == Sp::Puff ? 2.5f : 0.8f;
        particles_.emit(p);
    }
}

void App::event_particles(const sim::KartEvent& e) {
    using Sp = render::Renderer::Sprite;
    const sim::World* w = world();
    if (!w || e.kart < 0 || size_t(e.kart) >= w->karts().size()) return;
    const sim::KartState& k = w->karts()[size_t(e.kart)];
    const vec3 base = k.pos + k.up() * 0.2f;
    const vec3 front = base + k.forward() * 0.9f + k.up() * 0.3f;
    auto R = [&] { return particles_.rnd(); };
    // A shock ring: pops out flat and fades, the cartoon stand-in for an impact.
    auto ring = [&](vec3 pos, float size, vec3 color, float life, bool additive = true) {
        Particle p;
        p.pos = pos;
        p.life = life;
        p.size0 = size * 0.3f, p.size1 = size;
        p.sprite = Sp::Ring;
        p.additive = additive;
        p.color0 = vec4(color, 0.9f), p.color1 = vec4(color, 0.0f);
        particles_.emit(p);
    };
    // Motion arcs whirling round a point: tricks, spins, impacts.
    auto swooshes = [&](vec3 pos, int n, float radius, float size, vec3 color, float life, float rise) {
        for (int i = 0; i < n; ++i) {
            const float a = 2.0f * kPi * (float(i) + R() * 0.5f) / float(n);
            const vec3 side = glm::normalize(glm::cross(k.up(), vec3(std::cos(a), 0.0f, std::sin(a)) + vec3(1e-3f)));
            Particle p;
            p.pos = pos + side * radius;
            p.vel = side * (radius * 1.5f) + k.up() * rise;
            p.life = life * (0.7f + 0.5f * R());
            p.size0 = size, p.size1 = size * 0.5f;
            p.sprite = Sp::Swoosh;
            p.angle = a + kPi * 0.5f, p.spin = 4.0f;
            p.pop = 0.15f;
            p.color0 = vec4(color, 1.0f), p.color1 = vec4(color, 1.0f);
            p.drag = 2.0f;
            particles_.emit(p);
        }
    };
    // Little round sparks that fly out and fall: debris of a knock.
    auto sparks = [&](vec3 pos, int n, vec3 color, float speed, float life) {
        for (int i = 0; i < n; ++i) {
            Particle p;
            p.pos = pos;
            vec3 d = particles_.rnd_dir();
            d.y = std::abs(d.y) * 0.9f + 0.3f;
            p.vel = d * speed * (0.5f + 0.5f * R());
            p.life = life * (0.6f + 0.5f * R());
            p.size0 = p.size1 = 0.05f + 0.03f * R();
            p.sprite = Sp::Streak, p.stretch = 0.05f;
            p.additive = true;
            p.color0 = vec4(color, 1.0f), p.color1 = vec4(color, 0.0f);
            p.gravity = 14.0f, p.drag = 0.5f;
            particles_.emit(p);
        }
    };
    switch (e.type) {
    case sim::KartEventType::Land:
        burst(base, 7, vec3(1.0f, 1.0f, 1.0f), 3.5f, 0.2f, Sp::Puff2, 0.0f, 0.45f);
        ring(base + k.up() * 0.1f, 1.6f, vec3(1.0f, 1.0f, 1.0f), 0.3f, false);
        break;
    case sim::KartEventType::WallHit:
        ring(front, 1.4f, vec3(1.0f, 0.9f, 0.6f), 0.25f);
        burst(front, 5, vec3(0.9f, 0.9f, 0.92f), 3.0f, 0.16f, Sp::Puff, 0.0f, 0.4f);
        sparks(front, 8, vec3(1.0f, 0.85f, 0.35f), 6.0f, 0.4f);
        break;
    case sim::KartEventType::WaterEnter:
    case sim::KartEventType::WaterExit:
        burst(base + k.up() * 0.6f, 14, vec3(0.85f, 0.95f, 1.0f), 4.5f, 0.12f, Sp::Drop, 9.0f, 0.7f);
        burst(base + k.up() * 0.4f, 8, vec3(1.0f, 1.0f, 1.0f), 2.5f, 0.3f, Sp::Puff, 0.0f, 0.5f);
        ring(base + k.up() * 0.3f, 2.4f, vec3(0.8f, 0.95f, 1.0f), 0.35f, false);
        break;
    case sim::KartEventType::Bump:
        ring(base + k.up() * 0.7f, 1.2f, vec3(1.0f, 1.0f, 0.8f), 0.22f);
        burst(base + k.up() * 0.8f, 4, vec3(1.0f, 1.0f, 1.0f), 2.5f, 0.14f, Sp::Puff2, 0.0f, 0.35f);
        break;
    case sim::KartEventType::ItemBox: {
        const vec3 cs[5] = {{1.0f, 0.35f, 0.4f}, {0.3f, 0.65f, 1.0f}, {1.0f, 0.85f, 0.2f}, {0.4f, 0.95f, 0.5f}, {0.85f, 0.45f, 1.0f}};
        for (const vec3& c : cs) {
            burst(base + k.up() * 0.9f, 3, c, 5.5f, 0.06f, Sp::Confetti, 7.0f, 0.9f);
            burst(base + k.up() * 0.9f, 2, c, 4.0f, 0.07f, Sp::Dot, 0.0f, 0.5f);
        }
        ring(base + k.up() * 0.9f, 2.2f, vec3(0.7f, 0.9f, 1.0f), 0.35f);
        break;
    }
    case sim::KartEventType::Hit:
        if (e.kart < 12) hit_flash_[size_t(e.kart)] = 0.45f;
        ring(base + k.up() * 0.6f, 3.0f, vec3(1.0f, 0.95f, 0.85f), 0.3f);
        burst(base + k.up() * 0.5f, 10, vec3(1.0f, 1.0f, 1.0f), 4.0f, 0.3f, Sp::Puff, 0.0f, 0.5f);
        swooshes(base + k.up() * 0.8f, 6, 1.0f, 0.55f, vec3(1.0f, 1.0f, 1.0f), 0.45f, 1.5f);
        break;
    case sim::KartEventType::Explosion:
        if (e.kart < 12) hit_flash_[size_t(e.kart)] = 0.45f;
        burst(base, 6, vec3(0.25f, 0.22f, 0.22f), 3.0f, 0.4f, Sp::Puff, -1.5f, 0.9f); // soot off the kart
        break;
    case sim::KartEventType::Respawn:
        ring(base + k.up() * 0.3f, 2.6f, vec3(0.6f, 0.9f, 1.0f), 0.5f);
        burst(base + k.up() * 0.5f, 10, vec3(0.75f, 0.92f, 1.0f), 2.5f, 0.1f, Sp::Dot, -2.0f, 0.8f);
        break;
    case sim::KartEventType::Trick:
        ring(base + k.up() * 0.6f, 2.4f, vec3(1.0f, 0.9f, 0.5f), 0.3f);
        swooshes(base + k.up() * 0.6f, 8, 1.1f, 0.45f, vec3(1.0f, 0.95f, 0.75f), 0.4f, 0.6f);
        sparks(base + k.up() * 0.6f, 6, vec3(1.0f, 0.85f, 0.4f), 4.0f, 0.4f);
        break;
    case sim::KartEventType::Smash:
        burst(front, 5, vec3(0.92f, 0.82f, 0.62f), 3.0f, 0.2f, Sp::Puff, 0.0f, 0.55f);
        burst(front, 5, vec3(0.6f, 0.45f, 0.3f), 5.0f, 0.06f, Sp::Rock, 14.0f, 0.7f);
        ring(front, 1.6f, vec3(1.0f, 0.95f, 0.8f), 0.22f);
        break;
    case sim::KartEventType::TireKnock: burst(front, 4, vec3(0.8f, 0.8f, 0.82f), 2.5f, 0.18f, Sp::Puff2, 0.0f, 0.45f); break;
    case sim::KartEventType::TireBounce:
        ring(base, 1.4f, vec3(1.0f, 1.0f, 0.8f), 0.22f);
        burst(base, 4, vec3(1.0f, 1.0f, 1.0f), 2.5f, 0.14f, Sp::Puff2, 0.0f, 0.4f);
        break;
    case sim::KartEventType::Boost: { // the drift's pay-off keeps its spark star
        const int t = e.value == int(sim::BoostKind::MiniTurbo) ? 1 : e.value == int(sim::BoostKind::SuperMiniTurbo) ? 2
                    : e.value == int(sim::BoostKind::UltraMiniTurbo) ? 3 : 0;
        const vec3 back = base - k.forward() * 1.0f + k.up() * 0.3f;
        if (t > 0) {
            const vec3 c = vec3(to_tint(tier_color(t)));
            burst(back, 1, glm::mix(c, vec3(1.0f), 0.4f), 0.0f, 0.6f, Sp::Burst, 0.0f, 0.18f);
            burst(back, 8, c, 5.0f, 0.13f, Sp::Sparkle, 3.0f, 0.45f);
            ring(back, 1.8f, c, 0.25f);
        }
        if (e.value == int(sim::BoostKind::Slipstream)) {
            ring(base + k.up() * 0.5f, 2.0f, vec3(0.7f, 0.9f, 1.0f), 0.3f);
            sparks(base + k.up() * 0.5f, 6, vec3(0.85f, 0.97f, 1.0f), 3.0f, 0.4f);
        }
        break;
    }
    default: break;
    }
}

void App::explosion_burst(vec3 pos, float radius) {
    using Sp = render::Renderer::Sprite;
    auto R = [&] { return particles_.rnd(); };
    const float sz = radius / 6.5f; // tuned at the default radius
    // A flat shock ring on the ground and a flash in the air.
    for (int i = 0; i < 2; ++i) {
        Particle p;
        p.pos = pos + vec3(0, 0.3f + 0.5f * float(i), 0);
        p.life = 0.35f + 0.1f * float(i);
        p.size0 = 1.0f * sz, p.size1 = (7.0f + 2.0f * float(i)) * sz;
        p.sprite = Sp::Ring;
        p.additive = true;
        p.color0 = vec4(1.0f, 0.95f, 0.8f, 1.0f), p.color1 = vec4(1.0f, 0.5f, 0.2f, 0.0f);
        particles_.emit(p);
    }
    // Tongues of flame thrown outward.
    for (int i = 0; i < 16; ++i) {
        Particle p;
        p.pos = pos + vec3(0, 0.4f, 0);
        vec3 d = particles_.rnd_dir();
        d.y = std::abs(d.y) * 0.8f + 0.25f;
        p.vel = d * (7.0f + 5.0f * R()) * sz;
        p.carry = vec3(0.0f);
        p.life = 0.3f + 0.25f * R();
        p.size0 = 0.9f * sz, p.size1 = 0.2f * sz;
        p.sprite = Sp::Flame, p.stretch = 0.03f;
        p.additive = true;
        p.color0 = vec4(1.0f, 0.9f, 0.4f, 1.0f), p.color1 = vec4(1.0f, 0.35f, 0.1f, 0.6f);
        p.drag = 2.5f;
        particles_.emit(p);
    }
    // Thick dark smoke that billows up and pales as it thins.
    for (int i = 0; i < 22; ++i) {
        Particle p;
        vec3 d = particles_.rnd_dir();
        d.y = std::abs(d.y);
        p.pos = pos + d * 0.8f * sz + vec3(0, 0.3f, 0);
        p.vel = d * (2.0f + 3.0f * R()) * sz + vec3(0, 2.0f + 2.0f * R(), 0);
        p.life = 1.1f + 0.9f * R();
        p.size0 = (0.6f + 0.4f * R()) * sz, p.size1 = (1.8f + 0.8f * R()) * sz;
        p.sprite = R() < 0.5f ? Sp::Puff : Sp::Puff2;
        p.pop = 0.12f;
        p.angle = R() * 6.28f, p.spin = (R() - 0.5f) * 2.0f;
        p.color0 = vec4(0.22f, 0.2f, 0.21f, 1.0f), p.color1 = vec4(0.6f, 0.58f, 0.58f, 0.0f);
        p.gravity = -1.5f, p.drag = 1.8f;
        particles_.emit(p);
    }
    // Sparks and chunks of debris arcing out.
    for (int i = 0; i < 18; ++i) {
        Particle p;
        p.pos = pos + vec3(0, 0.5f, 0);
        vec3 d = particles_.rnd_dir();
        d.y = std::abs(d.y) * 1.2f + 0.3f;
        const bool rock = R() < 0.4f;
        p.vel = glm::normalize(d) * (6.0f + 7.0f * R()) * sz;
        p.life = 0.6f + 0.6f * R();
        p.size0 = p.size1 = rock ? 0.08f + 0.08f * R() : 0.06f;
        p.sprite = rock ? Sp::Rock : Sp::Streak;
        p.stretch = rock ? 0.0f : 0.06f;
        p.additive = !rock;
        p.spin = rock ? (R() - 0.5f) * 16.0f : 0.0f;
        p.color0 = rock ? vec4(0.3f, 0.26f, 0.24f, 1.0f) : vec4(1.0f, 0.85f, 0.4f, 1.0f);
        p.color1 = rock ? vec4(0.3f, 0.26f, 0.24f, 1.0f) : vec4(1.0f, 0.4f, 0.1f, 0.0f);
        p.gravity = 16.0f, p.drag = 0.4f;
        particles_.emit(p);
    }
}

render::MeshId App::item_mesh(sim::Item item) const {
    switch (item) {
    case sim::Item::Banana: case sim::Item::TripleBanana: return banana_mesh_;
    case sim::Item::GreenShell: case sim::Item::TripleGreen: return green_mesh_;
    case sim::Item::RedShell: case sim::Item::TripleRed: return red_mesh_;
    case sim::Item::Bomb: return bomb_mesh_;
    case sim::Item::OilSlick: return bomb_mesh_;
    default: return 0;
    }
}

const char* App::item_name(sim::Item item) {
    switch (item) {
    case sim::Item::Banana: case sim::Item::TripleBanana: return "banana";
    case sim::Item::GreenShell: case sim::Item::TripleGreen: return "green_shell";
    case sim::Item::RedShell: case sim::Item::TripleRed: return "red_shell";
    case sim::Item::Bomb: return "bomb";
    case sim::Item::OilSlick: return "oil_slick";
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

namespace {
// A mover's mesh in its own frame: x across, y up, z along the road.
render::MeshData make_mover_mesh(const sim::Track::Mover& m) {
    render::MeshBuilder b;
    auto face = [&](std::initializer_list<vec3> pts, vec3 out, uint32_t c) {
        std::vector<vec3> p(pts);
        if (glm::dot(glm::cross(p[1] - p[0], p[2] - p[0]), out) < 0.0f) std::reverse(p.begin(), p.end());
        if (p.size() == 3) b.tri(p[0], p[1], p[2], c);
        else b.quad(p[0], p[1], p[2], p[3], c);
    };
    const float hx = m.half.x, h = 2.0f * m.half.y, hz = m.half.z;
    if (m.kind == sim::Track::Mover::Kind::Block) {
        // A bumper block: red, a white band, a yellow cap.
        b.box({0, 0.5f * h, 0}, {hx, 0.5f * h, hz}, render::rgba(215, 55, 45));
        b.box({0, 0.5f * h, 0}, {hx + 0.04f, 0.12f * h, hz + 0.04f}, render::rgba(250, 250, 250));
        b.box({0, h - 0.08f, 0}, {hx + 0.06f, 0.1f, hz + 0.06f}, render::rgba(255, 205, 40));
        return b.take();
    }
    const float r = std::max(m.ramp, 0.01f);
    const bool boost = m.deck == sim::Surface::BoostPad;
    const uint32_t deck = boost ? render::rgba(255, 150, 40) : render::rgba(150, 162, 182), side = render::rgba(80, 92, 118),
                   ramp = boost ? render::rgba(240, 120, 30) : render::rgba(130, 140, 160);
    const float zs[4] = {-hz, -hz + r, hz - r, hz}, ys[4] = {0.0f, h, h, 0.0f};
    for (int i = 0; i < 3; ++i) {
        const vec3 up = glm::normalize(vec3(0, zs[i + 1] - zs[i], -(ys[i + 1] - ys[i])));
        face({{-hx, ys[i], zs[i]}, {hx, ys[i], zs[i]}, {hx, ys[i + 1], zs[i + 1]}, {-hx, ys[i + 1], zs[i + 1]}}, up, i == 1 ? deck : ramp);
    }
    for (float sx : {-1.0f, 1.0f}) {
        const float x = sx * hx;
        face({{x, 0, zs[1]}, {x, h, zs[1]}, {x, h, zs[2]}, {x, 0, zs[2]}}, {sx, 0, 0}, side);
        face({{x, 0, zs[0]}, {x, h, zs[1]}, {x, 0, zs[1]}}, {sx, 0, 0}, side);
        face({{x, 0, zs[2]}, {x, h, zs[2]}, {x, 0, zs[3]}}, {sx, 0, 0}, side);
        // Hazard stripes along both edges of the deck.
        const float w = 0.4f, seg = 0.7f;
        int k = 0;
        for (float z = zs[1]; z < zs[2] - 0.01f; z += seg, ++k) {
            const float z1 = std::min(z + seg, zs[2]);
            const float x0 = x - sx * w;
            face({{x0, h + 0.012f, z}, {x, h + 0.012f, z}, {x, h + 0.012f, z1}, {x0, h + 0.012f, z1}}, {0, 1, 0},
                 k % 2 ? render::rgba(30, 30, 36) : render::rgba(255, 200, 30));
        }
    }
    // Chevrons on the deck pointing both ways along it.
    for (float dz : {-1.0f, 1.0f})
        for (int i = 0; i < 2; ++i) {
            const float zc = dz * (hz - r - 1.2f - 1.0f * float(i)), y = h + 0.015f;
            face({{-hx * 0.45f, y, zc - dz * 0.6f}, {0, y, zc}, {0, y, zc + dz * 0.35f}, {-hx * 0.45f, y, zc - dz * 0.25f}}, {0, 1, 0},
                 render::rgba(240, 245, 255));
            face({{hx * 0.45f, y, zc - dz * 0.6f}, {0, y, zc}, {0, y, zc + dz * 0.35f}, {hx * 0.45f, y, zc - dz * 0.25f}}, {0, 1, 0},
                 render::rgba(240, 245, 255));
        }
    return b.take();
}
} // namespace

void App::draw_movers(const sim::World& w, float alpha) {
    const sim::Track& t = w.track();
    auto& meshes = mover_meshes_[t.id];
    if (meshes.size() != t.movers.size()) {
        meshes.clear();
        for (const auto& m : t.movers) meshes.push_back(renderer_.upload_mesh(make_mover_mesh(m)));
    }
    const double tick = double(w.tick_count()) - 1.0 + double(alpha);
    for (size_t i = 0; i < t.movers.size(); ++i) {
        const auto& m = t.movers[i];
        // -right: keep the frame right-handed so faces stay outward.
        const mat4 model(vec4(-m.right, 0.0f), vec4(m.up, 0.0f), vec4(m.forward, 0.0f), vec4(t.mover_pos(i, tick), 1.0f));
        renderer_.draw(meshes[i], model);
        if (m.kind == sim::Track::Mover::Kind::Platform)
            renderer_.draw(shadow_mesh_, glm::scale(glm::translate(model, vec3(0, 0.02f, 0)), vec3(m.half.x * 0.9f, 1.0f, m.half.z * 0.7f)));
    }
}

void App::update_lava(const sim::World* w, vec3 camera) {
    if (!lava_ || (lava_->pool_cells.empty() && lava_->zone_lines.empty())) return;
    const float clock = render::lava_clock(renderer_.shader_time());
    const sim::Track* gates = w ? &w->track() : shown_track_.get();
    auto zone_on = [&](int zone) {
        return zone < 0 || (size_t(zone) < gates->zones.size() && gates->gate_holds(gates->zones[size_t(zone)].gate));
    };
    // The lights breathe with the seams' glow; a zone's go out with it.
    for (size_t i = 0; i < lava_->lights.size() && lava_light_base_ + i < light_.lights.size(); ++i) {
        const auto& l = lava_->lights[i];
        light_.lights[lava_light_base_ + i].intensity = zone_on(l.zone) ? 1.1f * render::lava_pulse(clock, float(i) * 1.7f) : 0.0f;
    }
    if (paused_) return;
    // Splashes where a bubble pops near the camera.
    constexpr float kReach = 25.0f;
    const int x0 = int(std::floor((camera.x - kReach) / render::kLavaCell)), x1 = int(std::floor((camera.x + kReach) / render::kLavaCell));
    const int z0 = int(std::floor((camera.z - kReach) / render::kLavaCell)), z1 = int(std::floor((camera.z + kReach) / render::kLavaCell));
    for (int z = z0; z <= z1; ++z)
        for (int x = x0; x <= x1; ++x) {
            const render::LavaBubble bub = render::lava_bubble(x, z, clock);
            if (!bub.active) continue;
            // Is there lava under this bubble, and how high?
            float y = 0.0f;
            bool lava = lava_->pool_cells.count(render::LavaSpots::key(x, z)) > 0;
            if (lava) y = lava_->pool_y;
            for (size_t i = 0; i < lava_->zone_lines.size() && !lava; ++i) {
                const auto& ln = lava_->zone_lines[i];
                const vec3 d = vec3(bub.centre.x, ln.pos.y, bub.centre.y) - ln.pos;
                const float lat = glm::dot(d, ln.right);
                if (std::abs(glm::dot(d, ln.tangent)) < 1.1f && std::abs(lat) < ln.half && zone_on(ln.zone)) lava = true, y = ln.pos.y + lat * ln.right.y;
            }
            if (!lava) continue;
            const vec3 at(bub.centre.x, y + 0.1f, bub.centre.y);
            if (glm::distance(at, camera) > kReach) continue;
            const int64_t key = render::LavaSpots::key(x, z);
            const auto prev = lava_phase_.find(key);
            const bool popped = prev != lava_phase_.end() && bub.phase < prev->second - 0.5f;
            lava_phase_[key] = bub.phase;
            if (!popped) continue;
            using Sp = render::Renderer::Sprite;
            Particle p;
            p.pos = at;
            p.life = 0.3f;
            p.size0 = 0.6f, p.size1 = 2.2f;
            p.sprite = Sp::Burst;
            p.additive = true;
            p.color0 = vec4(1.0f, 0.75f, 0.3f, 1.0f), p.color1 = vec4(1.0f, 0.35f, 0.08f, 0.0f);
            particles_.emit(p);
            for (int k = 0; k < 3; ++k) {
                Particle s;
                s.pos = at;
                const float a = particles_.rnd() * 6.2831853f;
                s.vel = vec3(std::cos(a) * 2.0f, 4.0f + 3.0f * particles_.rnd(), std::sin(a) * 2.0f);
                s.gravity = 14.0f;
                s.life = 0.6f + 0.3f * particles_.rnd();
                s.size0 = 0.35f, s.size1 = 0.15f;
                s.sprite = Sp::Spark6;
                s.additive = true;
                s.angle = a;
                s.color0 = vec4(1.0f, 0.85f, 0.4f, 1.0f), s.color1 = vec4(1.0f, 0.3f, 0.05f, 0.0f);
                particles_.emit(s);
            }
        }
    // Forget cells left far behind.
    if (lava_phase_.size() > 2000) lava_phase_.clear();
}

void App::draw_water_overlay(const sim::World& w, vec3 camera) {
    const VkExtent2D ext = renderer_.ui_extent();
    const float W = float(ext.width), H = float(ext.height);
    const sim::Track& t = w.track();
    const int hint = size_t(player_) < w.karts().size() ? w.karts()[size_t(player_)].sample : -1;
    if (t.in_deep_water(camera, t.nearest_sample(camera, hint))) {
        // A blue-green tint, and light wavering down from the surface.
        renderer_.ui_rect(0, 0, W, H, render::rgba(20, 110, 125, 70));
        const float time = renderer_.shader_time();
        for (int i = 0; i < 5; ++i) {
            const float x = W * (0.1f + 0.2f * float(i)) + 18.0f * std::sin(time * 0.9f + float(i) * 1.7f);
            renderer_.ui_quad(vec2(x, 0.0f), vec2(x + 26.0f, 0.0f), vec2(x + 60.0f, H * 0.7f), vec2(x + 40.0f, H * 0.7f),
                              render::rgba(200, 245, 255, uint8_t(18 + 10 * std::sin(time * 1.3f + float(i)))));
        }
    }
    if (wet_lens_ > 0.0f) {
        // Drops running down the lens after surfacing, clearing in under a second.
        const float a = wet_lens_;
        for (int i = 0; i < 11; ++i) {
            const uint32_t h = uint32_t(i + 1) * 2654435761u;
            const float x = W * float(h % 1000) / 1000.0f, y0 = H * float((h >> 10) % 1000) / 1000.0f;
            const float y = y0 + (1.0f - a) * H * (0.08f + 0.06f * float((h >> 20) % 4));
            const float r = 10.0f + float((h >> 4) % 18);
            renderer_.ui_round_rect(x - r, y - r * 1.2f, r * 2.0f, r * 2.4f, r, render::rgba(210, 238, 255, uint8_t(90.0f * a)),
                                    render::rgba(150, 200, 230, uint8_t(60.0f * a)), r * 0.5f);
        }
        renderer_.ui_rect(0, 0, W, H, render::rgba(160, 210, 235, uint8_t(35.0f * a)));
        if (!paused_) wet_lens_ = std::max(0.0f, wet_lens_ - frame_dt_ / 0.9f);
    }
}

void App::draw_gates(const sim::World& w) {
    const sim::Track& t = w.track();
    const auto it = gate_meshes_.find(t.id);
    if (it == gate_meshes_.end()) return;
    const GateMeshes& g = it->second;
    for (const auto& [branch, meshes] : g.roads)
        if (t.branch_present(branch)) renderer_.draw(meshes.first, mat4(1.0f)), renderer_.draw(meshes.second, mat4(1.0f));
    // Zones on a timer flicker for the second before they come on: a warning.
    auto warning = [&](int gate) {
        if (gate < 0 || t.gate_holds(gate)) return false;
        const sim::Gate& gt = t.gates[size_t(gate)];
        if (gt.when != sim::Gate::When::Cycle) return false;
        const int period = std::max(gt.period, 1);
        const int phase = ((w.race_ticks() - gt.offset) % period + period) % period;
        const int until = gt.invert ? gt.open - phase : period - phase;
        return until > 0 && until <= 60 && (w.race_ticks() / 6) % 2 == 0;
    };
    for (size_t i = 0; i < t.zones.size() && i < g.zones.size(); ++i)
        if (t.gate_holds(t.zones[i].gate) || warning(t.zones[i].gate)) renderer_.draw(g.zones[i], mat4(1.0f));
    for (size_t i = 0; i < t.switches.size() && i < g.switches.size(); ++i) {
        const auto& sw = t.switches[i];
        const bool on = i < w.switches().size() && w.switches()[i] > 0;
        const mat4 model(vec4(-sw.right, 0.0f), vec4(sw.up, 0.0f), vec4(sw.forward, 0.0f), vec4(sw.pos, 1.0f));
        renderer_.draw(on ? g.switches[i].second : g.switches[i].first, model);
    }
    // Barriers rise out of the road and sink back into it over a moment.
    if (barrier_track_ != t.id || barrier_rise_.size() != t.barriers.size()) {
        barrier_track_ = t.id;
        barrier_rise_.assign(t.barriers.size(), 0.0f);
        for (size_t i = 0; i < t.barriers.size(); ++i) barrier_rise_[i] = t.barrier_up(t.barriers[i]) ? 1.0f : 0.0f;
    }
    for (size_t i = 0; i < t.barriers.size() && i < g.barriers.size(); ++i) {
        const auto& bar = t.barriers[i];
        float& rise = barrier_rise_[i];
        const float target = t.barrier_up(bar) ? 1.0f : 0.0f;
        rise += std::clamp(target - rise, -frame_dt_ * 2.5f, frame_dt_ * 4.0f);
        if (rise < 0.01f) continue;
        const float sink = (1.0f - rise) * (2.0f * bar.half.y + 0.2f);
        const mat4 model(vec4(-bar.right, 0.0f), vec4(bar.up, 0.0f), vec4(bar.forward, 0.0f), vec4(bar.pos - bar.up * sink, 1.0f));
        renderer_.draw(g.barriers[i], model);
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
                            const mat4 m(vec4(-r * (0.48f * cell.x * pop), 0.0f), vec4(o.up * (0.98f * cell.y * pop), 0.0f),
                                         vec4(f * (0.48f * cell.z * pop), 0.0f), vec4(c, 1.0f));
                            renderer_.draw(single_prop("crate"), fit_prop("crate", m));
                        }
            } else {
                const char* name = o.breakable == B::Barrel ? (i % 2 ? "barrel_red" : "barrel")
                                 : o.breakable == B::Crystal ? "crystal" : "tire_stack";
                const mat4 m(vec4(-o.right * (o.half.x * pop), 0.0f), vec4(o.up * (h * pop), 0.0f), vec4(o.forward * (o.half.z * pop), 0.0f),
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
                mat4 m(vec4(-o.right * r, 0.0f), vec4(o.up * 0.5f, 0.0f), vec4(o.forward * r, 0.0f), vec4(at, 1.0f));
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
        renderer_.set_fx(2); // iridescent, shimmering glass
        draw_item_mesh(box_mesh_, "item_box", glm::scale(m, vec3(scale)));
        renderer_.set_fx(0);
    }
    for (const sim::ItemEntity& e : w.entities()) {
        mat4 m = glm::translate(mat4(1.0f), e.pos - vec3(0, 0.3f, 0));
        const float yaw = std::atan2(e.vel.x, e.vel.z);
        switch (e.type) {
        case sim::EntityType::Banana: draw_item_mesh(banana_mesh_, "banana", m); break;
        case sim::EntityType::OilSlick: { // a puddle on the road, spreading out as it lands
            const float spread = std::min(1.0f, float(e.age) / 20.0f);
            renderer_.set_fx(6);
            renderer_.draw(oil_disc_mesh_, glm::scale(glm::rotate(glm::translate(mat4(1.0f), e.pos - vec3(0, 0.32f, 0)), float(e.owner) * 1.3f, vec3(0, 1, 0)),
                                                      vec3(2.2f * spread, 1.0f, 1.9f * spread)));
            renderer_.set_fx(0);
            break;
        }
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
            const float t = std::clamp(1.0f - float(e.life) / 30.0f, 0.0f, 1.0f);
            const float R = w.item_tuning().explosion_radius;
            if (e.age <= 2) { // new: burst once
                bool seen = false;
                for (const Blast& bl : blasts_) seen |= glm::distance(bl.pos, e.pos) < 1.5f;
                if (!seen) {
                    blasts_.push_back({e.pos, 0.0f});
                    explosion_burst(e.pos, R);
                }
            }
            // A cloud of fireballs that swell and burn away from the outside in.
            const float grow = 1.0f - (1.0f - t) * (1.0f - t);
            renderer_.set_fx(5);
            const vec3 offs[3] = {vec3(0.0f), vec3(0.45f, 0.55f, 0.2f), vec3(-0.4f, 0.75f, -0.3f)};
            for (int i = 0; i < 3; ++i) {
                const float p = std::clamp(t * 1.15f - 0.1f * float(i), 0.0f, 1.0f);
                const float r = R * (0.3f + 0.5f * grow) * (i == 0 ? 0.85f : 0.55f);
                renderer_.draw(fireball_mesh_, glm::scale(glm::translate(m, vec3(0, 0.3f + 0.6f * t, 0) + offs[i] * R * grow), vec3(r)), vec4(1.0f, 1.0f, 1.0f, p));
            }
            renderer_.set_fx(0);
            // Smoke keeps rolling out while it burns.
            if (!paused_ && t < 0.6f) {
                const float n = 30.0f * frame_dt_;
                for (int i = 0, c = int(n) + (particles_.rnd() < n - std::floor(n) ? 1 : 0); i < c; ++i) {
                    Particle p;
                    const vec3 d = particles_.rnd_dir();
                    p.pos = e.pos + vec3(0, 0.6f, 0) + d * 0.4f;
                    p.vel = vec3(d.x, std::abs(d.y) + 1.5f, d.z) * 2.5f;
                    p.life = 0.9f + 0.6f * particles_.rnd();
                    p.size0 = 0.5f, p.size1 = 1.5f;
                    p.sprite = particles_.rnd() < 0.5f ? render::Renderer::Sprite::Puff : render::Renderer::Sprite::Puff2;
                    p.pop = 0.15f;
                    p.spin = (particles_.rnd() - 0.5f) * 2.0f;
                    p.color0 = vec4(0.3f, 0.27f, 0.27f, 1.0f), p.color1 = vec4(0.65f, 0.62f, 0.62f, 0.0f);
                    p.gravity = -1.5f, p.drag = 1.5f;
                    particles_.emit(p);
                }
            }
            break;
        }
        }
    }
}

void App::render(float alpha, float dt) {
    frame_dt_ = std::clamp(dt, 0.0f, 0.1f);
    if (screen_ == Screen::Title || screen_ == Screen::Options || screen_ == Screen::Controls || screen_ == Screen::Online || screen_ == Screen::Course ||
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
    update_lava(w, cam.position);
    // Under deep water: a blue-green world that fades out close by.
    render::SceneLighting lit = light_;
    if (w && size_t(player_) < w->karts().size()) {
        const sim::Track& t = w->track();
        if (t.in_deep_water(cam.position, t.nearest_sample(cam.position, w->karts()[size_t(player_)].sample))) {
            lit.sky = lit.zenith = vec3(0.1f, 0.42f, 0.46f);
            lit.sky_texture = 0;
            lit.fog_density = 0.05f;
            lit.ambient = std::min(1.0f, lit.ambient + 0.1f);
        }
    }
    if (!renderer_.begin_frame(cam, lit)) return;
    if (track_mesh_) renderer_.draw(track_mesh_, mat4(1.0f));
    if (scenery_mesh_) renderer_.draw(scenery_mesh_, mat4(1.0f));
    if (prop_meshes_)
        for (render::MeshId id : *prop_meshes_) renderer_.draw(id, mat4(1.0f));
    if (oil_mesh_) renderer_.set_fx(6), renderer_.draw(oil_mesh_, mat4(1.0f)), renderer_.set_fx(0);
    if (w && screen_ == Screen::Standings) {
        for (size_t i = 0; i < w->karts().size(); ++i) draw_kart(int(i), w->karts()[i], alpha);
        draw_standings();
    } else if (w) {
        const auto& karts = w->karts();
        renderer_.ui_vignette(0.3f); // a lens feel; first, so the HUD sits on top
        draw_items(*w);
        draw_breakables(*w);
        draw_movers(*w, alpha);
        draw_gates(*w);
        for (size_t i = 0; i < karts.size(); ++i) draw_kart(int(i), karts[i], alpha);
        if (!paused_) {
            particles_.update(frame_dt_);
            for (float& f : hit_flash_) f = std::max(0.0f, f - frame_dt_);
            for (Blast& bl : blasts_) bl.age += frame_dt_;
            std::erase_if(blasts_, [](const Blast& bl) { return bl.age > 1.0f; });
        }
        // Skid marks fade over eight seconds.
        for (Skid& k : skids_) k.age += paused_ ? 0.0f : frame_dt_;
        std::erase_if(skids_, [](const Skid& k) { return k.age > 10.0f; });
        for (const Skid& k : skids_) {
            const float fade = std::clamp((10.0f - k.age) / 3.0f, 0.0f, 1.0f) * 0.55f;
            const vec2 uv[4] = {{0.0f, k.v0}, {1.0f, k.v0}, {1.0f, std::min(k.v1, 1.0f)}, {0.0f, std::min(k.v1, 1.0f)}};
            const vec4 c0(0.07f, 0.06f, 0.07f, k.a0 * fade), c1(0.07f, 0.06f, 0.07f, k.a1 * fade);
            const vec4 col[4] = {c0, c0, c1, c1};
            renderer_.decal(k.p, uv, col, render::Renderer::Sprite::Tread);
        }
        particles_.draw(renderer_);
        if (!online_) draw_ghost(alpha); // translucent, so after the solid karts
        const bool server_results = online_ && online_->results() && online_->phase() == net::RoomPhase::Results;
        if (size_t(player_) < karts.size()) draw_speed_lines(karts[size_t(player_)], w->params(player_).top_speed);
        draw_water_overlay(*w, cam.position);
        draw_hud(renderer_, *w, player_, kart_names(), paused_ ? HudMessage{} : message_, show_help_ && !server_results && !paused_, !server_results,
                 settings_.show_checkpoints || options_.checkpoints);
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
        case OptionsAction::Controls: screen_ = Screen::Controls; controls_ = {}; break;
        case OptionsAction::Back:
            if (const std::string sp = settings_path(); !sp.empty()) settings_.save(sp);
            screen_ = Screen::Title;
            break;
        case OptionsAction::None: break;
        }
        return;
    case Screen::Controls:
        if (controls_.waiting) {
            input_.capture_key = true;
            if (input_.captured_key) {
                if (input_.captured_key != SDL_SCANCODE_ESCAPE) {
                    auto& b = settings_.binds[size_t(controls_.row)];
                    b[size_t(controls_.slot)] = input_.captured_key;
                    apply_settings();
                    if (const std::string sp = settings_path(); !sp.empty()) settings_.save(sp);
                }
                input_.captured_key = 0;
                input_.capture_key = false;
                controls_.waiting = false;
            }
            return;
        }
        switch (update_controls(controls_, settings_, act)) {
        case ControlsAction::Changed:
            apply_settings();
            if (const std::string sp = settings_path(); !sp.empty()) settings_.save(sp);
            break;
        case ControlsAction::Back: screen_ = Screen::Options; break;
        default: break;
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
    if (track_mesh_) renderer_.draw(track_mesh_, mat4(1.0f));
    if (scenery_mesh_) renderer_.draw(scenery_mesh_, mat4(1.0f));
    if (prop_meshes_)
        for (render::MeshId id : *prop_meshes_) renderer_.draw(id, mat4(1.0f));
    const int mesh = online_ ? std::max(online_->seat(), 0) : std::clamp(options_.karts, 1, 12) - 1;
    const mat4 kart_m = glm::translate(mat4(1.0f), k.pos) * glm::mat4_cast(k.rot);
    renderer_.draw(shadow_mesh_, glm::scale(glm::translate(kart_m, vec3(0, 0.03f, -0.02f)), vec3(1.15f, 1.0f, 1.55f)));
    const GpuModel* preview = options_.preview_model.empty() ? nullptr : assets_.item(options_.preview_model);
    if (!preview && !options_.preview_model.empty()) preview = assets_.driver(options_.preview_model); // drivers too
    if (!preview && !options_.preview_model.empty() && props_.count(options_.preview_model)) { // and map props
        renderer_.draw(single_prop(options_.preview_model), kart_m);
    } else if (preview) {
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
    case Screen::Controls: draw_controls(renderer_, controls_, settings_); break;
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
    renderer_.ui_slant_rect(px, py, pw, 15.0f, ui::kBarSlant, render::rgba(0, 0, 0, 170), render::rgba(0, 0, 0, 170));
    const uint32_t dot = ping < 80 ? render::rgba(90, 230, 120) : ping < 160 ? render::rgba(255, 210, 60) : render::rgba(255, 90, 80);
    renderer_.ui_slant_rect(px + 6.0f, py + 4.0f, 6.0f, 7.0f, ui::kBarSlant, dot, dot);
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
