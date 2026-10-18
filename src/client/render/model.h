#pragma once
// Loaded models: Wavefront OBJ (with MTL textures) and COLLADA DAE files, as
// ripped from games, plus their PNG textures. Everything is read into plain
// MeshData parts grouped by texture, ready for Renderer::upload_mesh.
#include "mesh.h"

#include <optional>
#include <string>
#include <vector>

namespace gk::render {

struct ImageData {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba; // 8-bit, row-major, top row first
};
std::optional<ImageData> load_image(const std::string& path);

struct ModelPart {
    MeshData mesh;        // vertex colour white, UVs from the file (V flipped to top-down)
    std::string texture;  // resolved file path, or empty for an untextured part
    std::string material; // material name from the file, for the manifest
    bool two_sided = false;
};

struct ModelData {
    std::vector<ModelPart> parts;
    vec3 bounds_min{0.0f}, bounds_max{0.0f};
};

// Loads a .obj or .dae by extension. `error` gets a reason on failure.
std::optional<ModelData> load_model(const std::string& path, std::string* error = nullptr);

// Applies a transform to every part (positions and normals; winding flips
// with a mirroring transform) and recomputes the bounds.
void transform_model(ModelData& model, const mat4& m);

} // namespace gk::render
