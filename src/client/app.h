#pragma once
#include <SDL3/SDL_process.h>
#include "assets.h"
#include "render/kart_mesh.h"
#include "audio_device.h"
#include "hud.h"
#include "input.h"
#include "frontend.h"
#include "menu.h"
#include "render/renderer.h"

#include "gk/net/client_game.h"
#include "gk/sim/ghost.h"
#include "gk/net/transport.h"
#include "gk/sim/world.h"

#include <array>
#include <future>
#include <map>
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
    int spawn_obstacle = -1;      // testing: start lined up 14 m before this obstacle and drive straight at it
    int karts = 12;
    sim::EngineClass engine = sim::EngineClass::cc150;
    sim::Build build{1, 0, 0, 0};
    sim::CpuLevel cpu_level = sim::CpuLevel::Normal;
    std::string track;            // offline: race this track straight away (skips the menus)
    bool mute = false;
    bool time_trial = false;
    std::string menu;             // open on this menu (options, course) for screenshots
    bool host = false;            // start hosting a game straight away (the "Host a Server" shortcut)
    std::string preview_model;    // show this manifest item on the title screen instead of the kart
    std::string export_assets;    // write the built-in models as OBJ + PNG into this folder
    bool hide_ui = false;         // menus and HUD off, for screenshots      // with --track: a Time Trial instead of a VS race
    // Online play: connect to a gk_server instead of racing locally.
    std::string connect_host;
    uint16_t connect_port = net::kDefaultPort;
    std::string name = "Player";
    std::string master_url;       // master server for ONLINE (accounts, matchmaking)
    std::string region;           // preferred server region for matchmaking
    std::string crash;            // --crash segv|abort|throw: test the crash reporter
    net::LinkConditions link; // simulated lag and loss for testing
};

class App {
public:
    int run(const AppOptions& options);

private:
    bool init();
    void shutdown();
    void start_race();
    std::string race_track_id() const;
    void show_track(const std::string& id);
    void finish_offline_race();
    void update();
    void handle_events(const std::vector<sim::KartEvent>& events);
    void event_sound(const sim::KartEvent& e);
    void play_at(audio::Sfx s, int kart, float pitch = 1.0f);
    void update_audio();
    std::string ghost_path(const std::string& track, sim::EngineClass e) const;
    void finish_time_trial();
    void draw_ghost(float alpha);
    const sim::World* world() const { return online_ ? online_->world() : world_.get(); }
    void draw_net_status();
    void handle_menu_actions(const ClientActions& act, bool& running);
    void render_menu(float dt);
    void draw_online_results();
    void draw_standings();
    std::vector<std::string> kart_names() const;
    void update_camera(float alpha, float dt);
    void render(float alpha, float dt);
    void draw_kart(int index, const sim::KartState& s, float alpha);
    void draw_items(const sim::World& w);
    // Crates, barrels and tire stacks that break and come back.
    void draw_breakables(const sim::World& w);
    render::MeshId single_prop(const std::string& name);
    mat4 fit_prop(const std::string& name, const mat4& transform) const;
    void draw_held_item(sim::Item item, int uses, const mat4& kart, bool trailing);
    render::MeshId item_mesh(sim::Item item) const;
    static const char* item_name(sim::Item item);
    // Draws a loaded model if the asset pack has one under `name`, else the
    // procedural mesh.
    void draw_item_mesh(render::MeshId mesh, const char* name, const mat4& m, vec4 tint = vec4(1.0f));
    void draw_model(const GpuModel& model, const mat4& m, vec4 tint = vec4(1.0f));

    AppOptions options_;
    SDL_Window* window_ = nullptr;
    render::Renderer renderer_;
    InputMapper input_;
    AudioDevice audio_;
    int last_count_ = -1;      // countdown second last beeped
    int last_roulette_ = 0;
    std::string pref_dir_;     // per-user save folder (ghosts, settings)

    // Time Trials: the ghost raced against and the run being recorded.
    std::unique_ptr<sim::GhostRunner> ghost_;
    sim::KartState ghost_prev_;
    sim::Ghost recording_;
    int best_tick_ = 0;        // personal best on this track and class, 0 if none
    bool recording_done_ = false;
    sim::KartInput last_input_;

    sim::PhysicsTables tables_;
    sim::PartsCatalog parts_;
    sim::TrackCatalog tracks_;
    std::unique_ptr<sim::World> world_;            // local race
    std::unique_ptr<net::NetClient> net_;          // online race
    std::unique_ptr<net::ClientGame> online_;
    uint32_t online_race_ = 0;
    std::vector<sim::KartState> prev_;
    int player_ = 0;

    enum class Screen { Title, Options, Online, OnlineMenu, Join, Hosting, Course, Select, Race, Standings };
    OnlineMenuState online_menu_;
    std::string join_text_, join_hint_;
    // Hosting: the server this game started, and what friends type to join.
    SDL_Process* host_process_ = nullptr;
    double host_started_ = 0.0;
    std::vector<std::string> host_lines_;
    bool host_failed_ = false;
    std::string host_share_;  // shown in the lobby while hosting
    void start_hosting();
    void poll_hosting();
    void stop_hosting();
    void start_join();
    Screen screen_ = Screen::Title;
    TitleState title_;
    OptionsState options_menu_;
    Settings settings_;
    void apply_settings();

    // ONLINE: sign in to the master server and matchmake on a worker thread.
    struct MatchResult {
        std::string error; // empty on success
        std::string host, token, name;
        uint16_t port = 0;
        int rating = 0;
    };
    void start_matchmaking();
    void poll_matchmaking();
    bool connect_online(const std::string& host, uint16_t port, const std::string& token);
    void leave_online();
    std::future<MatchResult> matching_;
    std::future<void> crash_upload_; // last runs' crash reports, sent at launch
    std::string online_error_;
    std::string account_label_; // "NAME (rating)" once signed in
    bool transport_ready_ = false;
    std::string settings_path() const { return pref_dir_.empty() || options_.max_frames >= 0 ? std::string() : pref_dir_ + "settings.txt"; }
    CourseState course_;
    SelectState select_;
    float menu_angle_ = 0.0f;

    // Offline Grand Prix: race number in the cup and points per kart.
    struct GrandPrix {
        int race = 0;
        std::array<int, 12> points{};
        std::vector<ResultRow> rows; // last race, for the standings screen
        bool over = false;
    } gp_;

    // Track shown now (menu backdrop or race), its meshes and lighting.
    std::unique_ptr<sim::Track> shown_track_;
    std::map<std::string, std::pair<render::MeshId, render::MeshId>> track_meshes_;
    render::SceneLighting light_;

    render::MeshId track_mesh_ = 0, scenery_mesh_ = 0, spark_mesh_ = 0, flame_mesh_ = 0, shadow_mesh_ = 0;
    render::MeshId box_mesh_ = 0, banana_mesh_ = 0, green_mesh_ = 0, red_mesh_ = 0, spiked_mesh_ = 0, bomb_mesh_ = 0,
                   ball_mesh_ = 0, boomerang_mesh_ = 0, bullet_mesh_ = 0, chomper_mesh_ = 0;
    float flash_ = 0.0f; // white screen flash (lightning)
    // What a kart looks like: procedural pieces, each replaced by a model
    // from the asset pack when it has one.
    struct KartVisual {
        render::MeshId body = 0, driver = 0, wheels = 0, glider = 0;
        const GpuModel* body_model = nullptr;
        const GpuModel* driver_model = nullptr;
        const GpuModel* tire_model = nullptr;
        const GpuModel* glider_model = nullptr;
        render::KartLayout layout;
    };
    AssetLibrary assets_;
    std::map<uint32_t, GpuModel> gobby_karts_;         // the painted standard kart, by paint colour
    struct PropModel {
        render::MeshData mesh;
        render::TextureId texture = 0;
        bool two_sided = false, blended = false;
        vec3 lo{0.0f}, hi{0.0f};
        bool glow = false, unlit = false;
    };
    std::map<std::string, PropModel> props_;           // painted map props, merged per track
    std::map<std::string, std::vector<render::MeshId>> track_props_;
    const std::vector<render::MeshId>* prop_meshes_ = nullptr;
    std::map<std::string, render::MeshId> single_props_; // one copy, drawn per instance
    struct Debris {
        vec3 pos, vel, axis;
        float angle = 0.0f, spin = 0.0f, life = 0.0f, size = 0.2f;
        vec4 tint{1.0f};
    };
    std::vector<Debris> debris_;
    std::vector<uint16_t> prop_seen_down_;  // last drawn state per obstacle
    std::vector<float> prop_pop_;           // seconds since it came back
    render::MeshId chip_mesh_ = 0;
    uint32_t debris_seed_ = 1;
    std::vector<KartVisual> kart_visuals_;              // per kart in the race
    std::map<uint64_t, KartVisual> build_visuals_;      // (build, colour) -> visual
    std::array<float, 12> wheel_angle_{};               // tyre spin per kart, radians
    float frame_dt_ = 1.0f / 60.0f;
    const KartVisual& build_visual(const sim::Build& b, int slot);
    void draw_kart_visual(const KartVisual& v, const mat4& m, vec4 tint, float wheel_angle);
    void draw_glider_visual(const KartVisual& v, const mat4& m, vec4 tint);
    void set_kart_meshes(const std::vector<sim::Build>& builds);
    sim::Build ghost_build_;

    vec3 cam_pos_{0.0f}, cam_look_{0.0f};
    float cam_fov_ = 70.0f;
    bool cam_init_ = false;
    bool cam_back_ = false;
    vec3 cam_dir_{0.0f, 0.0f, 1.0f};
    float cam_height_ = 2.2f, cam_dist_ = 5.5f, cam_y_ = 0.0f;
    HudMessage message_;
    bool show_help_ = true;
    uint64_t frame_ = 0;
};

} // namespace gk
