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

// Builds meshes. The flat primitives (tri, quad, box, cone, icosphere) give
// every face its own vertices and normal; the smooth ones (grid and what is
// built on it: sphere, lathe, loft, capsule, tube, rounded_box) share
// vertices along a surface so the lighting rounds over it.
class MeshBuilder {
public:
    // Texture for the faces added from now on, mapped from the face's
    // position (seen along its most facing axis), one tile per `tile` metres.
    void texture(uint32_t layer, float tile = 2.0f) {
        layer_ = layer;
        tile_ = tile;
    }
    // Painted textures: every primitive added from now on maps its 0..1
    // texture coordinates into this region of the texture (smooth primitives
    // with tiles = 1, and the *_uv primitives' own coordinates).
    void uv_region(float x0, float y0, float x1, float y1) {
        region_[0] = x0;
        region_[1] = y0;
        region_[2] = x1;
        region_[3] = y1;
        has_region_ = true;
    }
    void uv_region_off() { has_region_ = false; }
    // A box whose six faces each map to the whole region.
    void box_painted(vec3 center, vec3 half, uint32_t color);

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

    // ---- smooth surfaces
    // A rows x cols grid of points, triangulated with averaged vertex normals.
    // With `wrap` the last column joins the first. Texture coordinates run
    // 0..u_tiles across the columns and 0..v_tiles down the rows.
    void grid(const std::vector<vec3>& points, int rows, int cols, bool wrap, uint32_t color, float u_tiles = 1.0f,
              float v_tiles = 1.0f);
    // Ellipsoid (a sphere when the radii match), `segments` around, `rings` from pole to pole.
    void sphere(vec3 center, vec3 radii, uint32_t color, int segments = 24, int rings = 16);
    void sphere(vec3 center, float radius, uint32_t color, int segments = 24, int rings = 16) {
        sphere(center, vec3(radius), color, segments, rings);
    }
    // A profile of (radius, height) pairs revolved around `axis` from `origin`.
    // Points with radius 0 close the ends.
    void lathe(vec3 origin, vec3 axis, const std::vector<vec2>& profile, int segments, uint32_t color, float u_tiles = 1.0f,
               float v_tiles = 1.0f);
    // Successive cross-section rings, all of the same point count, skinned
    // together; ends are closed with a flat fan when `cap_ends` is set.
    void loft(const std::vector<std::vector<vec3>>& rings, uint32_t color, bool cap_ends = true);
    void capsule(vec3 a, vec3 b, float radius, uint32_t color, int segments = 12, int rings = 4);
    // A tube from a to b with radii at each end, rounded or flat caps.
    void tube(vec3 a, vec3 b, float radius_a, float radius_b, uint32_t color, int segments = 16, bool caps = true);
    void rounded_box(vec3 center, vec3 half, float radius, uint32_t color, int segments = 16, int rings = 12);
    void torus(vec3 center, vec3 axis, float ring_radius, float tube_radius, uint32_t color, int segments = 24, int sides = 10);
    // A round tube along a polyline, with a radius at every point: pipes,
    // frames, fenders, handlebars.
    void sweep(const std::vector<vec3>& path, const std::vector<float>& radii, uint32_t color, int segments = 12, bool caps = true);
    void sweep(const std::vector<vec3>& path, float radius, uint32_t color, int segments = 12, bool caps = true) {
        sweep(path, std::vector<float>(path.size(), radius), color, segments, caps);
    }

    void append(const MeshData& other, const mat4& transform);

    MeshData& data() { return data_; }
    MeshData take() { return std::move(data_); }

private:
    vec2 project(vec3 p, vec3 n) const;
    vec2 regioned(vec2 uv) const {
        return has_region_ ? vec2(region_[0] + uv.x * (region_[2] - region_[0]), region_[1] + uv.y * (region_[3] - region_[1])) : uv;
    }
    MeshData data_;
    uint32_t layer_ = 0;
    float tile_ = 2.0f;
    float region_[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    bool has_region_ = false;
};

// Procedural placeholder content until the glTF pipeline lands.
MeshData make_ground_mesh(float half_size, int cells);
MeshData make_tree_mesh();

} // namespace gk::render
