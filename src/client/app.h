#pragma once
#include <SDL3/SDL_process.h>
#include "assets.h"
#include "render/kart_mesh.h"
#include "audio_device.h"
#include "hud.h"
#include "input.h"
#include "particles.h"
#include "frontend.h"
#include "menu.h"
#include "render/renderer.h"
#include "render/track_mesh.h"

#include "gk/net/client_game.h"
#include "gk/sim/ghost.h"
#include "gk/net/transport.h"
#include "gk/sim/world.h"

#include <array>
#include <future>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
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
    bool pixel_look = false;      // --pixel: the low-res look regardless of settings (screenshots)
    bool volumetric = false;      // --volumetric: sun shafts and mist on regardless of settings (screenshots)
    bool checkpoints = false;     // --checkpoints: checkpoint lines on the minimap regardless of settings (screenshots)
    int spawn_obstacle = -1;
    int spawn_sample = -1; // screenshots: start the player on this road sample (side roads too)
    int start_lap = 0;     // screenshots: everyone starts on this lap, for the track's lap gates
    std::string demo_fx;          // testing: "explosion", "hit", "tumble" or "trick" on the player at frame 250
    int give_item = 0;            // testing: hand the player this item (sim::Item value) at the start      // testing: start lined up 14 m before this obstacle and drive straight at it
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
    // The pause menu: offline it stops the race, online it only covers it.
    bool update_pause(const ClientActions& act, bool& running);
    std::vector<const char*> pause_items() const;
    void draw_pause();
    bool paused_ = false;
    int pause_row_ = 0;
    void render(float alpha, float dt);
    void draw_kart(int index, const sim::KartState& s, float alpha);
    void draw_items(const sim::World& w);
    // Crates, barrels and tire stacks that break and come back.
    void draw_breakables(const sim::World& w);
    void draw_movers(const sim::World& w, float alpha);
    void emit_kart_particles(int index, const sim::KartState& s, const mat4& m);
    void event_particles(const sim::KartEvent& e);
    void burst(vec3 pos, int n, vec3 color, float speed, float size, render::Renderer::Sprite sprite, float gravity, float life);
    Particles particles_;
    struct Skid {
        vec3 p[4];
        float v0, v1, a0, a1, age;
    };
    struct SkidPoint {
        bool valid = false;
        int wheels = 0;
        vec3 wheel[2]{};
        float width[2]{0.26f, 0.26f};
        vec3 across{1, 0, 0};
        float strength = 0.0f, v = 0.0f;
    };
    std::vector<Skid> skids_;
    std::vector<SkidPoint> skid_last_; // per kart
    // Speed lines: streaks at the edges of the screen when going fast.
    struct SpeedLine {
        float angle, r, len, life, age, width;
    };
    std::vector<SpeedLine> speed_lines_;
    void draw_speed_lines(const sim::KartState& k, float top_speed);
    std::map<std::string, std::vector<render::MeshId>> mover_meshes_; // per track, one per mover
    // What comes and goes during a race: roads that vanish, barriers that
    // rise and sink, switches, zones. Drawn from the world's gate state.
    void draw_gates(const sim::World& w);
    struct GateMeshes {
        std::vector<std::pair<int, std::pair<render::MeshId, render::MeshId>>> roads; // branch, (road, painted walls)
        std::vector<render::MeshId> barriers, zones;
        std::vector<std::pair<render::MeshId, render::MeshId>> switches; // up, pressed
    };
    std::map<std::string, GateMeshes> gate_meshes_;
    std::vector<float> barrier_rise_; // 0 sunk .. 1 standing, eased
    std::string barrier_track_;
    std::map<std::string, std::pair<render::TextureId, std::pair<vec3, vec3>>> sky_textures_; // theme -> sky, zenith, horizon
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

    enum class Screen { Title, Options, Controls, Online, OnlineMenu, Join, Hosting, Course, Select, Race, Standings };
    ControlsState controls_;
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
    std::map<std::string, std::vector<render::PointLight>> track_lights_; // lamps, neon, lanterns, lava per track
    const std::vector<render::PointLight>* lights_ = nullptr;
    // Lava on volcano tracks: where it shows, the lights along it (the tail
    // of light_.lights from lava_light_base_), and each bubble cell's last
    // phase near the camera, to splash where one pops.
    std::map<std::string, render::LavaSpots> lava_spots_;
    const render::LavaSpots* lava_ = nullptr;
    size_t lava_light_base_ = 0;
    std::unordered_map<int64_t, float> lava_phase_;
    void update_lava(const sim::World* w, vec3 camera);
    render::SceneLighting light_;

    render::MeshId track_mesh_ = 0, scenery_mesh_ = 0, spark_mesh_ = 0, flame_mesh_ = 0, shadow_mesh_ = 0, fireball_mesh_ = 0;
    render::MeshId oil_disc_mesh_ = 0, oil_mesh_ = 0; // a dropped slick, and the track's own spills
    std::map<std::string, render::MeshId> oil_meshes_;
    // Hit flash per kart: seconds left of the white-to-red glow.
    std::array<float, 12> hit_flash_{};
    // Explosions seen this second, so each one bursts once.
    struct Blast {
        vec3 pos;
        float age;
    };
    std::vector<Blast> blasts_;
    void explosion_burst(vec3 pos, float radius);
    // A flame from the flame shader: at `pos` (model space of `m`), pointing
    // along `dir`, `width` across and `length` long, in `color`.
    void draw_flame(const mat4& m, vec3 pos, vec3 dir, float width, float length, vec3 color, float phase);
    render::MeshId box_mesh_ = 0, banana_mesh_ = 0, green_mesh_ = 0, red_mesh_ = 0, spiked_mesh_ = 0, bomb_mesh_ = 0,
                   ball_mesh_ = 0, boomerang_mesh_ = 0, bullet_mesh_ = 0, chomper_mesh_ = 0;
    float flash_ = 0.0f; // white screen flash (lightning)
    float wet_lens_ = 0.0f; // drops on the lens after coming up out of deep water, 1 .. 0
    void draw_water_overlay(const sim::World& w, vec3 camera);
    // What a kart looks like: procedural pieces, each replaced by a model
    // from the asset pack when it has one.
    struct KartVisual {
        render::MeshId body = 0, driver = 0, wheels = 0, glider = 0;
        const GpuModel* body_model = nullptr;
        const GpuModel* driver_model = nullptr;
        const GpuModel* tire_model = nullptr;
        const GpuModel* glider_model = nullptr;
        render::KartLayout layout;
        bool gobby = false; // the painted standard kart: twin exhausts
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
    // Smoothed body motion per kart, so drifts swing in and out instead of
    // snapping: a critically damped spring on the nose yaw and the roll.
    struct KartPose {
        float yaw = 0.0f, yaw_v = 0.0f, roll = 0.0f, roll_v = 0.0f, pitch = 0.0f, pitch_v = 0.0f;
    };
    std::array<KartPose, 12> poses_{};
    static void spring(float& x, float& v, float target, float omega, float dt);
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
