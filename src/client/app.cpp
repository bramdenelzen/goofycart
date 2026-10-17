#include "app.h"

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
        if (act.quit_requested || act.pause_pressed) running = false;
        if (act.toggle_resolution_pressed) renderer_.set_full_resolution(!renderer_.full_resolution());
        if (act.toggle_help_pressed) show_help_ = !show_help_;
        if (act.restart_pressed && !online_) start_race();
        if (net_) net_->service([this](std::span<const uint8_t> d) { online_->on_message(d); });

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
    if (!tables_.load(sim::data_path("physics.json"), &err) || !parts_.load(sim::data_path("parts.json"), &err)) {
        log_error("Could not load game data: {}", err);
        return false;
    }
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
        online_->join(options_.name, options_.build);
    } else {
        start_race();
    }
    // Only the greybox track exists so far, online and offline.
    const sim::Track track = sim::make_greybox_track();
    track_mesh_ = renderer_.upload_mesh(render::make_track_mesh(track));
    scenery_mesh_ = renderer_.upload_mesh(render::make_scenery_mesh(track));
    glider_mesh_ = renderer_.upload_mesh(render::make_glider_mesh(render::rgba(250, 250, 250)));
    spark_mesh_ = renderer_.upload_mesh(render::make_spark_mesh());
    flame_mesh_ = renderer_.upload_mesh(render::make_flame_mesh());
    for (int i = 0; i < 12; ++i)
        kart_meshes_.push_back(renderer_.upload_mesh(
            render::make_kart_mesh(kKartColors[i], kKartColors[(i + 5) % 12])));
    log_info("Renderer ready, internal resolution {}x{}", renderer_.internal_extent().width,
             renderer_.internal_extent().height);
    return true;
}

void App::start_race() {
    sim::RaceConfig rc;
    rc.engine = options_.engine;
    world_ = std::make_unique<sim::World>(sim::make_greybox_track(), tables_, rc);
    auto clamp_part = [](int i, size_t n) { return std::clamp(i, 0, int(n) - 1); };
    options_.build.driver = clamp_part(options_.build.driver, parts_.drivers.size());
    options_.build.body = clamp_part(options_.build.body, parts_.bodies.size());
    options_.build.tires = clamp_part(options_.build.tires, parts_.tires.size());
    options_.build.glider = clamp_part(options_.build.glider, parts_.gliders.size());
    const int karts = std::clamp(options_.karts, 1, 12);
    // The player starts at the back of the grid, as in a Grand Prix opener.
    player_ = karts - 1;
    for (int i = 0; i < karts; ++i) {
        sim::Build b = options_.build;
        if (i != player_) b = {i % int(parts_.drivers.size()), i % int(parts_.bodies.size()), (i / 2) % int(parts_.tires.size()), 0};
        world_->add_kart(tables_.params(parts_.levels(b), parts_.body(b), rc.engine));
    }
    prev_ = world_->karts();
    cam_init_ = false;
    message_ = {};
}

void App::update() {
    if (online_) {
        const sim::World* w = online_->world();
        if (online_->race_id_changed(online_race_)) {
            cam_init_ = false;
            message_ = {};
        }
        player_ = std::max(online_->seat(), 0);
        if (w) prev_ = w->karts();
        sim::KartInput in = input_.sample();
        if (w && online_->seat() >= 0 && options_.autodrive) {
            in = net::bot_input(w->karts()[size_t(player_)], w->track(), 7);
            if (!w->racing()) in.accelerate = w->countdown_left() < 80;
        }
        online_->step(in);
        handle_events(online_->take_events());
        if (message_.time_left > 0.0f) message_.time_left -= kDt;
        return;
    }
    prev_ = world_->karts();
    const auto& karts = world_->karts();
    std::vector<sim::KartInput> inputs(karts.size());
    for (size_t i = 0; i < karts.size(); ++i) {
        if (int(i) == player_ && !options_.autodrive) {
            inputs[i] = input_.sample();
        } else {
            // Placeholder CPU drivers until the AI phase: line followers with
            // slightly different look-ahead so they spread out.
            inputs[i] = net::bot_input(karts[i], world_->track(), int(i));
            if (int(i) == player_ && world_->countdown_left() > 0) inputs[i].accelerate = world_->countdown_left() < 80;
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
    vec4 tint(1.0f);
    if (s.respawn_frames > 0 || (s.invincible_frames > 0 && (frame_ / 4) % 2)) tint = vec4(1, 1, 1, 1) * 0.6f + 0.4f;
    renderer_.draw(kart_meshes_[size_t(index % 12)], m, tint);
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

void App::render(float alpha, float dt) {
    const sim::World* w = world();
    if (w && size_t(player_) < w->karts().size()) update_camera(dt);
    render::Camera cam;
    cam.position = cam_init_ ? cam_pos_ : vec3(0, 12, -25);
    cam.target = cam_init_ ? cam_look_ : vec3(0, 0, 20);
    cam.fov_y_deg = cam_fov_;
    render::SceneLighting light;
    light.fog_density = 0.0035f;
    if (!renderer_.begin_frame(cam, light)) return;
    renderer_.draw(track_mesh_, mat4(1.0f));
    renderer_.draw(scenery_mesh_, mat4(1.0f));
    if (w) {
        const auto& karts = w->karts();
        for (size_t i = 0; i < karts.size(); ++i) draw_kart(int(i), karts[i], alpha);
        draw_hud(renderer_, *w, player_, message_, show_help_);
    }
    if (online_) draw_net_status();
    renderer_.end_frame();
}

void App::draw_net_status() {
    const VkExtent2D ext = renderer_.internal_extent();
    const float H = float(ext.height);
    std::string line;
    if (!net_->connected()) line = "DISCONNECTED FROM SERVER";
    else if (!online_->joined() || !online_->world()) line = "CONNECTING...";
    else if (!online_->has_clock()) line = "WAITING FOR THE SERVER...";
    else if (online_->seat() < 0) line = "SPECTATING - YOU JOIN THE NEXT RACE";
    if (!line.empty()) {
        const float wdt = render::Renderer::ui_text_width(line.c_str(), 2.0f);
        renderer_.ui_text((float(ext.width) - wdt) * 0.5f, H * 0.35f, line.c_str(), render::rgba(255, 255, 255), 2.0f);
    }
    char buf[96];
    std::snprintf(buf, sizeof(buf), "ONLINE  PING %d MS%s", net_->rtt_ms(), online_->data_mismatch() ? "  DATA MISMATCH" : "");
    renderer_.ui_text((float(ext.width) - render::Renderer::ui_text_width(buf)) * 0.5f, 4.0f, buf,
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
