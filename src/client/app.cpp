#include "app.h"

#include "render/item_mesh.h"
#include "render/track_mesh.h"

#include "gk/core/fixed_step.h"
#include "gk/core/log.h"
#include "gk/sim/data.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace gk {
namespace {

constexpr uint32_t kKartColors[12] = {
    render::rgba(220, 50, 40),  render::rgba(40, 110, 230), render::rgba(60, 190, 70),  render::rgba(250, 200, 30),
    render::rgba(160, 70, 210), render::rgba(250, 130, 30), render::rgba(30, 200, 200), render::rgba(240, 100, 170),
    render::rgba(240, 240, 240), render::rgba(70, 70, 80),  render::rgba(150, 100, 60), render::rgba(120, 200, 255),
};

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
        if (act.toggle_resolution_pressed) renderer_.set_full_resolution(!renderer_.full_resolution());
        if (act.toggle_help_pressed) show_help_ = !show_help_;
        if (net_) net_->service([this](std::span<const uint8_t> d) { online_->on_message(d); });
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
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        log_error("SDL_Init failed: {}", SDL_GetError());
        return false;
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

    if (!options_.connect_host.empty()) {
        if (!net::transport_init()) return false;
        net_ = std::make_unique<net::NetClient>();
        net_->set_conditions(options_.link);
        if (!net_->connect(options_.connect_host, options_.connect_port)) return false;
        online_ = std::make_unique<net::ClientGame>(tables_, [this](uint8_t ch, std::vector<uint8_t> d, bool rel) {
            net_->send(ch, std::move(d), rel);
        });
        online_->set_tracks(tracks_);
        online_->join(options_.name, options_.build);
        screen_ = Screen::Select;
    }
    select_.build = options_.build;
    // --track (and demos) race straight away, skipping the menus.
    if (!online_ && (options_.autodrive || !options_.track.empty())) {
        course_.mode = GameMode::Versus;
        if (options_.track.empty()) options_.track = tracks_.tracks.empty() ? "greybox" : tracks_.tracks.front().id;
        if (const int i = tracks_.find(options_.track); i >= 0) course_.track = i;
        start_race();
        screen_ = Screen::Race;
    } else {
        show_track(course_track(course_, tracks_));
    }
    glider_mesh_ = renderer_.upload_mesh(render::make_glider_mesh(render::rgba(250, 250, 250)));
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
    for (int i = 0; i < 12; ++i)
        kart_meshes_.push_back(renderer_.upload_mesh(
            render::make_kart_mesh(kKartColors[i], kKartColors[(i + 5) % 12])));
    log_info("Renderer ready, internal resolution {}x{}", renderer_.internal_extent().width,
             renderer_.internal_extent().height);
    return true;
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
        const render::MeshId track = renderer_.upload_mesh(render::make_track_mesh(*shown_track_));
        const render::MeshId scenery = renderer_.upload_mesh(render::make_scenery_mesh(*shown_track_));
        it = track_meshes_.emplace(shown_track_->id, std::pair(track, scenery)).first;
    }
    track_mesh_ = it->second.first;
    scenery_mesh_ = it->second.second;
    const render::ThemeStyle& th = render::theme_style(shown_track_->theme);
    light_ = {};
    light_.sky = th.sky;
    light_.fog_density = th.fog_density;
    light_.ambient = th.ambient;
}

void App::start_race() {
    sim::RaceConfig rc;
    rc.engine = course_.engine;
    const std::string id = race_track_id();
    show_track(id);
    sim::Track track = sim::make_track(tracks_, id);
    const bool time_trial = course_.mode == GameMode::TimeTrial;
    if (time_trial) track.item_boxes.clear(); // just the three mushrooms
    message_ = {track.name, render::rgba(255, 255, 255), 3.0f};
    world_ = std::make_unique<sim::World>(std::move(track), tables_, rc);
    auto clamp_part = [](int i, size_t n) { return std::clamp(i, 0, int(n) - 1); };
    options_.build.driver = clamp_part(options_.build.driver, parts_.drivers.size());
    options_.build.body = clamp_part(options_.build.body, parts_.bodies.size());
    options_.build.tires = clamp_part(options_.build.tires, parts_.tires.size());
    options_.build.glider = clamp_part(options_.build.glider, parts_.gliders.size());
    const int karts = time_trial ? 1 : std::clamp(options_.karts, 1, 12);
    // The player starts at the back of the grid, as in a Grand Prix opener.
    player_ = karts - 1;
    for (int i = 0; i < karts; ++i) {
        sim::Build b = options_.build;
        if (i != player_) b = {i % int(parts_.drivers.size()), i % int(parts_.bodies.size()), (i / 2) % int(parts_.tires.size()), 0};
        sim::KartParams p = tables_.params(parts_.levels(b), parts_.body(b), rc.engine);
        if (i != player_) p.rubber_band = sim::cpu_rubber_band(course_.cpu);
        world_->add_kart(p);
    }
    if (time_trial) world_->give_item(player_, sim::Item::TripleMushroom);
    prev_ = world_->karts();
    cam_init_ = false;
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
            cam_init_ = false;
            message_ = {};
        }
        // Spectators follow the leader.
        player_ = online_->seat() >= 0 ? online_->seat() : (w && !w->karts().empty() ? w->standings()[0] : 0);
        if (w) prev_ = w->karts();
        sim::KartInput in = input_.sample();
        if (w && online_->seat() >= 0 && options_.autodrive) {
            in = net::bot_input(*w, player_, sim::CpuLevel::Hard);
        }
        online_->step(in);
        handle_events(online_->take_events());
        if (message_.time_left > 0.0f) message_.time_left -= kDt;
        return;
    }
    if (!world_ || screen_ != Screen::Race) return;
    prev_ = world_->karts();
    const auto& karts = world_->karts();
    std::vector<sim::KartInput> inputs(karts.size());
    for (size_t i = 0; i < karts.size(); ++i) {
        if (int(i) == player_ && !options_.autodrive) {
            inputs[i] = input_.sample();
        } else {
            inputs[i] = net::bot_input(*world_, int(i), int(i) == player_ ? sim::CpuLevel::Hard : options_.cpu_level);
        }
    }
    world_->tick(inputs);
    handle_events(world_->events());
    if (message_.time_left > 0.0f) message_.time_left -= kDt;
}

void App::handle_events(const std::vector<sim::KartEvent>& events) {
    for (const auto& e : events) {
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

void App::update_camera(float dt) {
    const sim::KartState& now = world()->karts()[size_t(player_)];
    const vec3 fwd = now.forward();
    vec3 flat = vec3(fwd.x, 0.0f, fwd.z);
    flat = glm::length(flat) > 0.01f ? glm::normalize(flat) : vec3(0, 0, 1);
    const bool back = input_.actions().look_back;
    const float dist = now.gliding ? 7.5f : 5.5f;
    const float height = now.gliding ? 3.2f : 2.2f;
    const vec3 desired = now.pos + (back ? flat : -flat) * dist + kUp * height;
    const vec3 look = now.pos + (back ? -flat : flat) * 3.0f + kUp * 0.9f;
    if (!cam_init_ || back) {
        cam_pos_ = desired;
        cam_look_ = look;
        cam_init_ = true;
    } else {
        const float k = 1.0f - std::exp(-8.0f * dt);
        cam_pos_ = glm::mix(cam_pos_, desired, k);
        cam_look_ = glm::mix(cam_look_, look, 1.0f - std::exp(-14.0f * dt));
    }
    const float target_fov = now.boost_frames > 0 ? 82.0f : 72.0f;
    cam_fov_ += (target_fov - cam_fov_) * (1.0f - std::exp(-5.0f * dt));
}

void App::draw_kart(int index, const sim::KartState& s, float alpha) {
    const sim::KartState& p = prev_.size() > size_t(index) ? prev_[size_t(index)] : s;
    vec3 pos = glm::mix(p.pos, s.pos, alpha);
    if (online_) pos += online_->visual_offset(index);
    const quat rot = glm::slerp(p.rot, s.rot, alpha);
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
    if (s.bullet_frames > 0) renderer_.draw(bullet_mesh_, base);
    else renderer_.draw(kart_meshes_[size_t(index % 12)], m, tint);
    if (s.chomper_frames > 0) {
        const float lunge = s.chomper_cooldown > world()->item_tuning().chomper_cooldown - 10 ? 0.8f : 0.0f;
        renderer_.draw(chomper_mesh_, glm::translate(base, vec3(0, 0.3f, 1.1f + lunge)));
    }
    if (s.item != sim::Item::None && s.roulette == 0 && s.bullet_frames == 0)
        draw_held_item(s.item, s.item_uses, base, s.trailing);
    if (s.gliding) renderer_.draw(glider_mesh_, m, to_tint(kKartColors[size_t(index % 12)]));

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

void App::draw_held_item(sim::Item item, int uses, const mat4& kart, bool trailing) {
    const render::MeshId mesh = item_mesh(item);
    if (!mesh) return;
    const bool triple = item == sim::Item::TripleBanana || item == sim::Item::TripleGreen || item == sim::Item::TripleRed;
    if (triple) {
        // Orbiting the kart.
        for (int i = 0; i < uses; ++i) {
            const float a = float(frame_) * 0.08f + float(i) * 2.0f * kPi / 3.0f;
            renderer_.draw(mesh, glm::translate(kart, vec3(std::cos(a) * 1.3f, 0.2f, std::sin(a) * 1.3f)));
        }
    } else if (trailing) {
        renderer_.draw(mesh, glm::translate(kart, vec3(0, 0.05f, -1.4f)));
    }
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
        renderer_.draw(box_mesh_, glm::scale(m, vec3(scale)));
    }
    for (const sim::ItemEntity& e : w.entities()) {
        mat4 m = glm::translate(mat4(1.0f), e.pos - vec3(0, 0.3f, 0));
        const float yaw = std::atan2(e.vel.x, e.vel.z);
        switch (e.type) {
        case sim::EntityType::Banana: renderer_.draw(banana_mesh_, m); break;
        case sim::EntityType::GreenShell: renderer_.draw(green_mesh_, glm::rotate(m, float(e.age) * 0.4f, vec3(0, 1, 0))); break;
        case sim::EntityType::RedShell: renderer_.draw(red_mesh_, glm::rotate(m, float(e.age) * 0.4f, vec3(0, 1, 0))); break;
        case sim::EntityType::SpikedShell:
            renderer_.draw(spiked_mesh_, glm::rotate(glm::scale(m, vec3(1.4f)), float(e.age) * 0.3f, vec3(0, 1, 0)));
            break;
        case sim::EntityType::Bomb: {
            // Flashes faster as the fuse runs down.
            const bool lit = e.life > 0 && (e.life < 40 ? (e.age / 3) % 2 : (e.age / 10) % 2);
            renderer_.draw(bomb_mesh_, glm::rotate(m, yaw, vec3(0, 1, 0)), lit ? vec4(2.5f, 1.2f, 1.0f, 1.0f) : vec4(1.0f));
            break;
        }
        case sim::EntityType::Fireball:
            renderer_.draw(ball_mesh_, glm::translate(m, vec3(0, 0.3f, 0)), vec4(1.6f, 0.7f, 0.2f, 1.0f));
            break;
        case sim::EntityType::Boomerang:
            renderer_.draw(boomerang_mesh_, glm::rotate(glm::translate(m, vec3(0, 0.3f, 0)), float(e.age) * 0.6f, vec3(0, 1, 0)));
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
    if (screen_ == Screen::Title || screen_ == Screen::Course || screen_ == Screen::Select) {
        render_menu(dt);
        return;
    }
    const sim::World* w = world();
    if (w && size_t(player_) < w->karts().size()) update_camera(dt);
    render::Camera cam;
    cam.position = cam_init_ ? cam_pos_ : vec3(0, 12, -25);
    cam.target = cam_init_ ? cam_look_ : vec3(0, 0, 20);
    cam.fov_y_deg = cam_fov_;
    if (w) show_track(w->track().id);
    if (!renderer_.begin_frame(cam, light_)) return;
    renderer_.draw(track_mesh_, mat4(1.0f));
    renderer_.draw(scenery_mesh_, mat4(1.0f));
    if (w && screen_ == Screen::Standings) {
        for (size_t i = 0; i < w->karts().size(); ++i) draw_kart(int(i), w->karts()[i], alpha);
        draw_standings();
    } else if (w) {
        const auto& karts = w->karts();
        draw_items(*w);
        for (size_t i = 0; i < karts.size(); ++i) draw_kart(int(i), karts[i], alpha);
        const bool server_results = online_ && online_->results() && online_->phase() == net::RoomPhase::Results;
        draw_hud(renderer_, *w, player_, kart_names(), message_, show_help_ && !server_results, !server_results);
        if (server_results) draw_online_results();
        if (!online_ && karts[size_t(player_)].finished) {
            const char* hint = "ENTER / A: CONTINUE";
            const VkExtent2D ext = renderer_.internal_extent();
            renderer_.ui_text((float(ext.width) - render::Renderer::ui_text_width(hint, 1.5f)) * 0.5f,
                              float(ext.height) - 22.0f, hint, render::rgba(255, 255, 255), 1.5f);
        }
    }
    if (flash_ > 0.0f) {
        const VkExtent2D ext = renderer_.internal_extent();
        renderer_.ui_rect(0, 0, float(ext.width), float(ext.height), render::rgba(255, 255, 230, uint8_t(flash_ * 200.0f)));
        flash_ = std::max(0.0f, flash_ - dt * 3.0f);
    }
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

void App::handle_menu_actions(const ClientActions& act, bool& running) {
    if (online_) {
        // The server's phase decides the screen: lobby -> select, otherwise race.
        const bool lobby = online_->has_lobby() ? online_->phase() == net::RoomPhase::Lobby : !online_->world();
        const Screen want = lobby ? Screen::Select : Screen::Race;
        if (want != screen_) {
            screen_ = want;
            select_.ready = false;
        }
        if (screen_ == Screen::Race) {
            if (act.pause_pressed) running = false;
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
        case TitleChoice::Quit: running = false; return;
        case TitleChoice::None: return;
        }
        course_.row = 0;
        screen_ = Screen::Course;
        show_track(course_track(course_, tracks_));
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
        if (act.back) {
            screen_ = options_.track.empty() ? Screen::Course : Screen::Title;
            options_.track.clear();
            cam_init_ = false;
            show_track(course_track(course_, tracks_));
        } else if (finished && act.accept) {
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
    // Look past the kart so it sits right of centre, clear of the menu panel.
    cam.target = k.pos + kUp * 0.5f - glm::normalize(glm::cross(kUp, orbit)) * 1.3f;
    cam.fov_y_deg = 55.0f;
    if (!renderer_.begin_frame(cam, light_)) return;
    renderer_.draw(track_mesh_, mat4(1.0f));
    renderer_.draw(scenery_mesh_, mat4(1.0f));
    const int mesh = online_ ? std::max(online_->seat(), 0) : std::clamp(options_.karts, 1, 12) - 1;
    renderer_.draw(kart_meshes_[size_t(mesh % 12)], glm::translate(mat4(1.0f), k.pos) * glm::mat4_cast(k.rot));
    switch (screen_) {
    case Screen::Title: draw_title(renderer_, title_); break;
    case Screen::Course: draw_course(renderer_, course_, tracks_); break;
    default: {
        const net::LobbyMsg* lobby = online_ && online_->has_lobby() ? &online_->lobby() : nullptr;
        std::string where = shown_track_->name;
        if (lobby && !lobby->cup.empty()) where = lobby->cup + ": " + where;
        else if (!online_ && course_.mode == GameMode::GrandPrix && !tracks_.cups.empty())
            where = tracks_.cups[size_t(course_.cup % int(tracks_.cups.size()))].name + ": " + shown_track_->name;
        draw_select(renderer_, select_, parts_, lobby, where);
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
    const VkExtent2D ext = renderer_.internal_extent();
    const float H = float(ext.height);
    std::string line;
    if (!net_->connected()) line = "DISCONNECTED FROM SERVER";
    else if (!online_->joined()) line = "CONNECTING...";
    else if (screen_ == Screen::Race && (!online_->world() || !online_->has_clock())) line = "WAITING FOR THE SERVER...";
    else if (screen_ == Screen::Race && online_->seat() < 0 && online_->phase() == net::RoomPhase::Racing)
        line = "SPECTATING - YOU JOIN THE NEXT RACE";
    if (!line.empty()) {
        const float wdt = render::Renderer::ui_text_width(line.c_str(), 2.0f);
        renderer_.ui_text((float(ext.width) - wdt) * 0.5f, H * 0.35f, line.c_str(), render::rgba(255, 255, 255), 2.0f);
    }
    char buf[96];
    std::snprintf(buf, sizeof(buf), "ONLINE  PING %d MS%s", net_->rtt_ms(), online_->data_mismatch() ? "  DATA MISMATCH" : "");
    renderer_.ui_text(float(ext.width) - render::Renderer::ui_text_width(buf) - 6.0f, 46.0f, buf,
                      render::rgba(200, 230, 255), 1.0f);
}

void App::shutdown() {
    if (net_) {
        net_.reset();
        net::transport_shutdown();
    }
    renderer_.shutdown();
    if (window_) SDL_DestroyWindow(window_);
    window_ = nullptr;
    SDL_Quit();
}

} // namespace gk
