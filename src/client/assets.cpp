#include "assets.h"

#include "gk/core/log.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace gk {
namespace {

using json = nlohmann::json;

ModelSpec spec_from(const json& j, const std::string& default_fit, float default_size, const std::string& default_anchor) {
    ModelSpec s;
    s.fit = default_fit;
    s.size = default_size;
    s.anchor = default_anchor;
    if (j.is_string()) {
        s.file = j.get<std::string>();
        return s;
    }
    if (j.contains("file") && j["file"].is_array()) {
        for (const auto& f : j["file"]) {
            if (!f.is_string()) continue;
            if (s.file.empty()) s.file = f.get<std::string>();
            else s.extra_files.push_back(f.get<std::string>());
        }
    } else {
        s.file = j.value("file", "");
    }
    s.texture = j.value("texture", "");
    s.yaw = j.value("yaw", 0.0f);
    s.pitch = j.value("pitch", 0.0f);
    s.roll = j.value("roll", 0.0f);
    s.scale = j.value("scale", 1.0f);
    s.anchor = j.value("anchor", s.anchor);
    s.two_sided = j.value("two_sided", false);
    s.alpha = j.value("alpha", 1.0f);
    if (j.contains("tint") && j["tint"].is_array() && j["tint"].size() == 3)
        s.tint = vec3(j["tint"][0].get<float>(), j["tint"][1].get<float>(), j["tint"][2].get<float>());
    for (const char* dim : {"length", "width", "height", "radius"})
        if (j.contains(dim) && j[dim].is_number()) {
            s.fit = dim;
            s.size = j[dim].get<float>();
        }
    if (j.value("fit", "") == "none") s.fit = "none";
    if (j.contains("offset") && j["offset"].is_array() && j["offset"].size() == 3)
        s.offset = vec3(j["offset"][0].get<float>(), j["offset"][1].get<float>(), j["offset"][2].get<float>());
    if (j.contains("wheels") && j["wheels"].is_array())
        for (const auto& w : j["wheels"])
            if (w.is_array() && w.size() >= 2) {
                const float x = w[0].get<float>(), z = w[1].get<float>();
                const float y = w.size() >= 3 ? w[2].get<float>() : 0.0f;
                s.wheels.push_back(vec3(x, y, z));
            }
    if (j.contains("seat") && j["seat"].is_array() && j["seat"].size() == 3) {
        s.has_seat = true;
        s.seat = vec3(j["seat"][0].get<float>(), j["seat"][1].get<float>(), j["seat"][2].get<float>());
    }
    s.wheel_radius = j.value("wheel_radius", 0.0f);
    return s;
}

} // namespace

GpuModel AssetLibrary::upload(const ModelSpec& spec, const std::string& dir, render::Renderer& renderer) {
    GpuModel out;
    if (spec.file.empty()) return out;
    const std::string path = spec.file.rfind('/', 0) == 0 ? spec.file : dir + "/" + spec.file;
    std::string error;
    auto model = render::load_model(path, &error);
    if (!model) {
        log_warn("Model {}: {}", path, error);
        return out;
    }
    for (const std::string& extra : spec.extra_files) {
        const std::string epath = extra.rfind('/', 0) == 0 ? extra : dir + "/" + extra;
        if (auto more = render::load_model(epath, &error)) {
            for (auto& part : more->parts) model->parts.push_back(std::move(part));
        } else {
            log_warn("Model {}: {}", epath, error);
        }
    }
    if (!spec.texture.empty()) {
        const std::string tpath = spec.texture.rfind('/', 0) == 0 ? spec.texture : std::filesystem::path(path).parent_path().string() + "/" + spec.texture;
        for (auto& part : model->parts) part.texture = tpath;
    }
    // Orientation first, then fit the chosen dimension, then anchor.
    mat4 rot(1.0f);
    rot = glm::rotate(rot, glm::radians(spec.yaw), vec3(0, 1, 0));
    rot = glm::rotate(rot, glm::radians(spec.pitch), vec3(1, 0, 0));
    rot = glm::rotate(rot, glm::radians(spec.roll), vec3(0, 0, 1));
    render::transform_model(*model, rot);
    const vec3 ext = model->bounds_max - model->bounds_min;
    float scale = spec.scale;
    if (spec.fit == "length" && ext.z > 1e-6f) scale *= spec.size / ext.z;
    else if (spec.fit == "width" && ext.x > 1e-6f) scale *= spec.size / ext.x;
    else if (spec.fit == "height" && ext.y > 1e-6f) scale *= spec.size / ext.y;
    else if (spec.fit == "radius" && std::max(ext.y, ext.z) > 1e-6f) scale *= 2.0f * spec.size / std::max(ext.y, ext.z);
    mat4 fit = glm::scale(mat4(1.0f), vec3(scale));
    render::transform_model(*model, fit);
    vec3 shift(0.0f);
    const vec3 centre = 0.5f * (model->bounds_min + model->bounds_max);
    if (spec.anchor == "bottom") shift = vec3(-centre.x, -model->bounds_min.y, -centre.z);
    else if (spec.anchor == "center") shift = -centre;
    render::transform_model(*model, glm::translate(mat4(1.0f), shift + spec.offset));

    const uint32_t vertex_color = render::rgba(uint8_t(std::clamp(spec.tint.x, 0.0f, 1.0f) * 255.0f), uint8_t(std::clamp(spec.tint.y, 0.0f, 1.0f) * 255.0f),
                                               uint8_t(std::clamp(spec.tint.z, 0.0f, 1.0f) * 255.0f), uint8_t(std::clamp(spec.alpha, 0.0f, 1.0f) * 255.0f));
    for (auto& part : model->parts) {
        if (vertex_color != render::rgba(255, 255, 255))
            for (auto& v : part.mesh.vertices) v.color = vertex_color;
        render::TextureId tex = 0;
        if (!part.texture.empty()) {
            auto it = textures_.find(part.texture);
            if (it == textures_.end()) {
                if (auto img = render::load_image(part.texture)) {
                    tex = renderer.upload_texture(img->width, img->height, img->rgba.data());
                } else {
                    log_warn("Texture {} could not be read", part.texture);
                }
                it = textures_.emplace(part.texture, tex).first;
            }
            tex = it->second;
        }
        out.meshes.push_back(renderer.upload_mesh(part.mesh, tex, spec.two_sided || part.two_sided));
    }
    out.bounds_min = model->bounds_min;
    out.bounds_max = model->bounds_max;
    size_t tris = 0;
    for (const auto& p : model->parts) tris += p.mesh.indices.size() / 3;
    log_info("Model {}: {} parts, {} triangles, {:.2f} x {:.2f} x {:.2f} m", spec.file, model->parts.size(), tris,
             out.bounds_max.x - out.bounds_min.x, out.bounds_max.y - out.bounds_min.y, out.bounds_max.z - out.bounds_min.z);
    return out;
}

bool AssetLibrary::load(const std::string& dir, render::Renderer& renderer) {
    std::ifstream f(dir + "/manifest.json");
    if (!f) return false;
    json j;
    try {
        j = json::parse(f, nullptr, true, true);
    } catch (const std::exception& e) {
        log_warn("{}/manifest.json: {}", dir, e.what());
        return false;
    }
    struct Category {
        const char* key;
        Table* table;
        const char* fit;
        float size;
        const char* anchor;
    };
    const Category categories[] = {
        {"drivers", &drivers_, "height", 1.1f, "bottom"},
        {"bodies", &bodies_, "length", 1.9f, "bottom"},
        {"tires", &tires_, "radius", 0.26f, "center"},
        {"gliders", &gliders_, "width", 2.6f, "bottom"},
        {"items", &items_, "height", 0.6f, "bottom"},
    };
    for (const Category& c : categories) {
        if (!j.contains(c.key) || !j[c.key].is_object()) continue;
        for (const auto& [name, value] : j[c.key].items()) {
            const ModelSpec spec = spec_from(value, c.fit, c.size, c.anchor);
            (*c.table)[name] = upload(spec, dir, renderer);
            if (c.table == &bodies_) body_specs_[name] = spec;
        }
    }
    loaded_ = true;
    return true;
}

} // namespace gk
