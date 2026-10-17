#pragma once
#include "hud.h"
#include "input.h"
#include "menu.h"
#include "render/renderer.h"

#include "gk/net/client_game.h"
#include "gk/net/transport.h"
#include "gk/sim/world.h"

#include <memory>
#include <string>
#include <vector>

namespace gk {

struct AppOptions {
    int window_width = 1280;
    int window_height = 720;
    bool full_resolution = false;
    bool validation = false;
    int max_frames = -1;          // quit after this many frames (CI smoke tests)
    std::string screenshot_path;  // saved on the last frame when max_frames is set
    bool autodrive = false;       // the player kart drives itself (demos, screenshots)
    int karts = 12;
    sim::EngineClass engine = sim::EngineClass::cc150;
    sim::Build build{1, 0, 0, 0};
    // Online play: connect to a gk_server instead of racing locally.
    std::string connect_host;
    uint16_t connect_port = net::kDefaultPort;
    std::string name = "Player";
    net::LinkConditions link; // simulated lag and loss for testing
};

class App {
public:
    int run(const AppOptions& options);

private:
    bool init();
    void shutdown();
    void start_race();
    void update();
    void handle_events(const std::vector<sim::KartEvent>& events);
    const sim::World* world() const { return online_ ? online_->world() : world_.get(); }
    void draw_net_status();
    void handle_menu_actions(const ClientActions& act, bool& running);
    void render_menu(float dt);
    void draw_online_results();
    std::vector<std::string> kart_names() const;
    void update_camera(float dt);
    void render(float alpha, float dt);
    void draw_kart(int index, const sim::KartState& s, float alpha);
    void draw_items(const sim::World& w);
    void draw_held_item(sim::Item item, int uses, const mat4& kart, bool trailing);
    render::MeshId item_mesh(sim::Item item) const;

    AppOptions options_;
    SDL_Window* window_ = nullptr;
    render::Renderer renderer_;
    InputMapper input_;

    sim::PhysicsTables tables_;
    sim::PartsCatalog parts_;
    std::unique_ptr<sim::World> world_;            // local race
    std::unique_ptr<net::NetClient> net_;          // online race
    std::unique_ptr<net::ClientGame> online_;
    uint32_t online_race_ = 0;
    std::vector<sim::KartState> prev_;
    int player_ = 0;

    enum class Screen { Select, Race };
    Screen screen_ = Screen::Select;
    SelectState select_;
    std::unique_ptr<sim::Track> menu_track_;
    float menu_angle_ = 0.0f;

    render::MeshId track_mesh_ = 0, scenery_mesh_ = 0, glider_mesh_ = 0, spark_mesh_ = 0, flame_mesh_ = 0;
    render::MeshId box_mesh_ = 0, banana_mesh_ = 0, green_mesh_ = 0, red_mesh_ = 0, spiked_mesh_ = 0, bomb_mesh_ = 0,
                   ball_mesh_ = 0, boomerang_mesh_ = 0, bullet_mesh_ = 0, chomper_mesh_ = 0;
    float flash_ = 0.0f; // white screen flash (lightning)
    std::vector<render::MeshId> kart_meshes_;

    vec3 cam_pos_{0.0f}, cam_look_{0.0f};
    float cam_fov_ = 70.0f;
    bool cam_init_ = false;
    HudMessage message_;
    bool show_help_ = true;
    uint64_t frame_ = 0;
};

} // namespace gk
