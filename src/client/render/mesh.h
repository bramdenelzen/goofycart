#pragma once
#include "gk/core/math.h"

#include <cstdint>
#include <vector>

namespace gk::render {

// 40-byte vertex: flat-shaded faces with a vertex colour, multiplied by a
// low-res texture layer (textures.h; layer 0 is plain).
struct Vertex {
    vec3 pos;
    vec3 normal;
    uint32_t color; // RGBA8, R in the low byte
    vec2 uv{0.0f};
    uint32_t layer = 0;
};
static_assert(sizeof(Vertex) == 40);

constexpr uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    return uint32_t(r) | (uint32_t(g) << 8) | (uint32_t(b) << 16) | (uint32_t(a) << 24);
}

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

// Builds faceted meshes: every face gets its own vertices and a flat normal,
// which is the look the art direction asks for.
class MeshBuilder {
public:
    // Texture for the faces added from now on, mapped from the face's
    // position (seen along its most facing axis), one tile per `tile` metres.
    void texture(uint32_t layer, float tile = 2.0f) {
        layer_ = layer;
        tile_ = tile;
    }
    // A triangle or quad with its own texture coordinates (for textures with
    // a direction, or that follow a curve).
    void tri_uv(vec3 a, vec3 b, vec3 c, vec2 ua, vec2 ub, vec2 uc, uint32_t color);
    void quad_uv(vec3 a, vec3 b, vec3 c, vec3 d, vec2 ua, vec2 ub, vec2 uc, vec2 ud, uint32_t color);
    void tri(vec3 a, vec3 b, vec3 c, uint32_t color);           // counter-clockwise = front
    void quad(vec3 a, vec3 b, vec3 c, vec3 d, uint32_t color);  // a-b-c-d counter-clockwise
    void box(vec3 center, vec3 half, uint32_t color);
    // Axis-aligned along X, `sides` segments around the axis.
    void cylinder_x(vec3 center, float radius, float half_width, int sides, uint32_t color);
    void cone(vec3 base_center, float radius, float height, int sides, uint32_t color);
    void icosphere(vec3 center, float radius, uint32_t color); // 80 faces
    void append(const MeshData& other, const mat4& transform);

    MeshData& data() { return data_; }
    MeshData take() { return std::move(data_); }

private:
    vec2 project(vec3 p, vec3 n) const;
    MeshData data_;
    uint32_t layer_ = 0;
    float tile_ = 2.0f;
};

// Procedural placeholder content until the glTF pipeline lands.
MeshData make_ground_mesh(float half_size, int cells);
MeshData make_tree_mesh();

} // namespace gk::render
