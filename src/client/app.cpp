#include "app.h"

#include "gk/core/fixed_step.h"
#include "gk/core/log.h"

#include <cmath>

namespace gk {

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
        if (input_.actions().quit_requested || input_.actions().pause_pressed) running = false;
        if (input_.actions().toggle_resolution_pressed) renderer_.set_full_resolution(!renderer_.full_resolution());

        const uint64_t now = SDL_GetPerformanceCounter();
        const double elapsed = double(now - last) / freq;
        last = now;
        // Scripted runs step exactly one tick per frame so output is reproducible.
        const int ticks = options_.max_frames >= 0 ? 1 : step.advance(elapsed);
        for (int i = 0; i < ticks; ++i) update(kDt);

        if (options_.max_frames >= 0 && int(frame_) == options_.max_frames - 1 && !options_.screenshot_path.empty())
            renderer_.request_screenshot(options_.screenshot_path);
        render();
        ++frame_;
        if (options_.max_frames >= 0 && int(frame_) >= options_.max_frames) running = false;
    }
    shutdown();
    return 0;
}

bool App::init() {
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

    kart_mesh_ = renderer_.upload_mesh(render::make_kart_mesh(render::rgba(220, 50, 40), render::rgba(40, 90, 220)));
    ground_mesh_ = renderer_.upload_mesh(render::make_ground_mesh(60.0f, 30));
    tree_mesh_ = renderer_.upload_mesh(render::make_tree_mesh());
    log_info("Renderer ready, internal resolution {}x{}", renderer_.internal_extent().width,
             renderer_.internal_extent().height);
    return true;
}

void App::shutdown() {
    renderer_.shutdown();
    if (window_) SDL_DestroyWindow(window_);
    window_ = nullptr;
    SDL_Quit();
}

void App::update(float dt) {
    const sim::KartInput in = input_.sample();
    kart_yaw_ -= in.steer * 2.5f * dt;
    orbit_ += 0.3f * dt;
}

void App::render() {
    render::Camera cam;
    cam.position = vec3(std::sin(orbit_) * 5.0f, 2.2f, std::cos(orbit_) * 5.0f);
    cam.target = vec3(0, 0.6f, 0);
    render::SceneLighting light;
    if (!renderer_.begin_frame(cam, light)) return;
    renderer_.draw(ground_mesh_, mat4(1.0f));
    renderer_.draw(kart_mesh_, glm::rotate(mat4(1.0f), kart_yaw_, kUp));
    for (int i = 0; i < 16; ++i) {
        const float a = float(i) / 16.0f * 2.0f * kPi;
        const float r = 14.0f + 6.0f * float(i % 3);
        renderer_.draw(tree_mesh_, glm::translate(mat4(1.0f), vec3(std::cos(a) * r, 0, std::sin(a) * r)));
    }
    renderer_.end_frame();
}

} // namespace gk
