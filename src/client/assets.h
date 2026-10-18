#pragma once
// Optional model packs: a folder with a manifest.json that maps parts (by
// their ids in data/parts.json) and items to model files, which replace the
// procedural meshes when present. The folder is not part of the repository.
#include "render/gobby_assets.h"
#include "render/model.h"
#include "render/renderer.h"

#include <map>
#include <string>
#include <vector>

namespace gk {

struct GpuModel {
    std::vector<render::MeshId> meshes;
    vec3 bounds_min{0.0f}, bounds_max{0.0f};
    bool valid() const { return !meshes.empty(); }
};

// How a model is fitted into the game's metres: rotated, scaled to a size,
// then anchored and offset.
struct ModelSpec {
    std::string file;
    std::vector<std::string> extra_files; // merged into the same model (same units)
    std::string texture;                  // replaces every part's texture (e.g. a red shell's)
    float yaw = 0.0f, pitch = 0.0f, roll = 0.0f; // degrees, applied before fitting
    std::string fit = "height";                    // length (z), width (x), height (y), radius (half of the larger of y/z), none
    float size = 1.0f;                             // metres for the fitted dimension
    float scale = 1.0f;                            // extra multiplier
    std::string anchor = "bottom";                 // bottom (min y at 0, xz centred), center, none
    vec3 offset{0.0f};
    bool two_sided = false;
    vec3 tint{1.0f};                               // multiplies the texture
    float alpha = 1.0f;                            // < 1 draws the model translucent
    // Bodies only: optional wheel centres and seat (metres, after fitting).
    std::vector<vec3> wheels;
    bool has_seat = false;
    vec3 seat{0.0f};
    float wheel_radius = 0.0f;
};

class AssetLibrary {
public:
    // Loads <dir>/manifest.json and every model it names. Missing files are
    // logged and skipped; the procedural meshes stay for those parts.
    bool load(const std::string& dir, render::Renderer& renderer);
    bool loaded() const { return loaded_; }
    // The game's own painted models, before any pack overrides them.
    void add_builtin_item(const std::string& name, const render::PaintedModel& model, render::Renderer& renderer);
    void add_builtin_tire(const std::string& part_id, const render::PaintedModel& model, render::Renderer& renderer) {
        tires_[part_id] = upload_painted(model, renderer);
    }
    void add_builtin_driver(const std::string& part_id, const render::PaintedModel& model, render::Renderer& renderer) {
        drivers_[part_id] = upload_painted(model, renderer);
    }
    static GpuModel upload_painted(const render::PaintedModel& model, render::Renderer& renderer);

    const GpuModel* driver(const std::string& part_id) const { return find(drivers_, part_id); }
    const GpuModel* body(const std::string& part_id) const { return find(bodies_, part_id); }
    const GpuModel* tire(const std::string& part_id) const { return find(tires_, part_id); }
    const GpuModel* glider(const std::string& part_id) const { return find(gliders_, part_id); }
    const GpuModel* item(const std::string& name) const { return find(items_, name); }
    const ModelSpec* body_spec(const std::string& part_id) const {
        auto it = body_specs_.find(part_id);
        return it == body_specs_.end() ? nullptr : &it->second;
    }

private:
    using Table = std::map<std::string, GpuModel>;
    static const GpuModel* find(const Table& t, const std::string& key) {
        auto it = t.find(key);
        return it != t.end() && it->second.valid() ? &it->second : nullptr;
    }
    GpuModel upload(const ModelSpec& spec, const std::string& dir, render::Renderer& renderer);
    std::map<std::string, render::TextureId> textures_; // by resolved path
    Table drivers_, bodies_, tires_, gliders_, items_;
    std::map<std::string, ModelSpec> body_specs_;
    bool loaded_ = false;
};

} // namespace gk
