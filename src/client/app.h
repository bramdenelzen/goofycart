#pragma once
#include "input.h"
#include "render/renderer.h"

#include <string>

namespace gk {

struct AppOptions {
    int window_width = 1280;
    int window_height = 720;
    bool full_resolution = false;
    bool validation = false;
    int max_frames = -1;          // quit after this many frames (CI smoke tests)
    std::string screenshot_path;  // saved on the last frame when max_frames is set
};

class App {
public:
    int run(const AppOptions& options);

private:
    bool init();
    void shutdown();
    void update(float dt);
    void render();

    AppOptions options_;
    SDL_Window* window_ = nullptr;
    render::Renderer renderer_;
    InputMapper input_;

    render::MeshId kart_mesh_ = 0, ground_mesh_ = 0, tree_mesh_ = 0;
    float kart_yaw_ = 0.0f;
    float orbit_ = 0.0f;
    uint64_t frame_ = 0;
};

} // namespace gk
