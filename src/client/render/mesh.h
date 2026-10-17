#pragma once
#include "gk/core/math.h"

#include <cstdint>
#include <vector>

namespace gk::render {

// 28-byte vertex: low-poly art is flat-shaded with vertex colours, so there is
// no UV or tangent data yet.
struct Vertex {
    vec3 pos;
    vec3 normal;
    uint32_t color; // RGBA8, R in the low byte
};
static_assert(sizeof(Vertex) == 28);

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
    void tri(vec3 a, vec3 b, vec3 c, uint32_t color);           // counter-clockwise = front
    void quad(vec3 a, vec3 b, vec3 c, vec3 d, uint32_t color);  // a-b-c-d counter-clockwise
    void box(vec3 center, vec3 half, uint32_t color);
    // Axis-aligned along X, `sides` segments around the axis.
    void cylinder_x(vec3 center, float radius, float half_width, int sides, uint32_t color);
    void cone(vec3 base_center, float radius, float height, int sides, uint32_t color);
    void icosphere(vec3 center, float radius, uint32_t color); // 20 faces
    void append(const MeshData& other, const mat4& transform);

    MeshData& data() { return data_; }
    MeshData take() { return std::move(data_); }

private:
    MeshData data_;
};

// Procedural placeholder content until the glTF pipeline lands.
MeshData make_ground_mesh(float half_size, int cells);
MeshData make_tree_mesh();

} // namespace gk::render
