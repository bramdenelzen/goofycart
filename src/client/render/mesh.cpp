#include "mesh.h"

#include <cmath>

namespace gk::render {

void MeshBuilder::tri(vec3 a, vec3 b, vec3 c, uint32_t color) {
    vec3 n = glm::cross(b - a, c - a);
    const float len = glm::length(n);
    n = len > 1e-12f ? n / len : kUp;
    const auto base = static_cast<uint32_t>(data_.vertices.size());
    data_.vertices.push_back({a, n, color});
    data_.vertices.push_back({b, n, color});
    data_.vertices.push_back({c, n, color});
    data_.indices.insert(data_.indices.end(), {base, base + 1, base + 2});
}

void MeshBuilder::quad(vec3 a, vec3 b, vec3 c, vec3 d, uint32_t color) {
    vec3 n = glm::cross(b - a, c - a);
    const float len = glm::length(n);
    n = len > 1e-12f ? n / len : kUp;
    const auto base = static_cast<uint32_t>(data_.vertices.size());
    data_.vertices.push_back({a, n, color});
    data_.vertices.push_back({b, n, color});
    data_.vertices.push_back({c, n, color});
    data_.vertices.push_back({d, n, color});
    data_.indices.insert(data_.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
}

void MeshBuilder::box(vec3 c, vec3 h, uint32_t color) {
    const vec3 p[8] = {
        c + vec3(-h.x, -h.y, -h.z), c + vec3(h.x, -h.y, -h.z), c + vec3(h.x, h.y, -h.z), c + vec3(-h.x, h.y, -h.z),
        c + vec3(-h.x, -h.y, h.z),  c + vec3(h.x, -h.y, h.z),  c + vec3(h.x, h.y, h.z),  c + vec3(-h.x, h.y, h.z),
    };
    quad(p[4], p[5], p[6], p[7], color); // +z
    quad(p[1], p[0], p[3], p[2], color); // -z
    quad(p[5], p[1], p[2], p[6], color); // +x
    quad(p[0], p[4], p[7], p[3], color); // -x
    quad(p[7], p[6], p[2], p[3], color); // +y
    quad(p[0], p[1], p[5], p[4], color); // -y
}

void MeshBuilder::cylinder_x(vec3 c, float r, float hw, int sides, uint32_t color) {
    for (int i = 0; i < sides; ++i) {
        const float a0 = 2.0f * kPi * float(i) / float(sides);
        const float a1 = 2.0f * kPi * float(i + 1) / float(sides);
        const vec3 o0(0.0f, std::cos(a0) * r, std::sin(a0) * r);
        const vec3 o1(0.0f, std::cos(a1) * r, std::sin(a1) * r);
        const vec3 l(-hw, 0, 0), rr(hw, 0, 0);
        quad(c + l + o0, c + l + o1, c + rr + o1, c + rr + o0, color);
        tri(c + rr, c + rr + o0, c + rr + o1, color);
        tri(c + l, c + l + o1, c + l + o0, color);
    }
}

void MeshBuilder::cone(vec3 b, float r, float h, int sides, uint32_t color) {
    const vec3 tip = b + vec3(0, h, 0);
    for (int i = 0; i < sides; ++i) {
        const float a0 = 2.0f * kPi * float(i) / float(sides);
        const float a1 = 2.0f * kPi * float(i + 1) / float(sides);
        const vec3 p0 = b + vec3(std::cos(a0) * r, 0, std::sin(a0) * r);
        const vec3 p1 = b + vec3(std::cos(a1) * r, 0, std::sin(a1) * r);
        if (h >= 0.0f) {
            tri(p1, p0, tip, color);
            tri(b, p0, p1, color);
        } else { // pointing down: flip the winding so faces stay outward
            tri(p0, p1, tip, color);
            tri(b, p1, p0, color);
        }
    }
}

void MeshBuilder::icosphere(vec3 c, float r, uint32_t color) {
    const float t = (1.0f + std::sqrt(5.0f)) / 2.0f;
    vec3 v[12] = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t},
                  {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
    for (auto& p : v) p = c + glm::normalize(p) * r;
    const int f[20][3] = {{0, 11, 5}, {0, 5, 1},  {0, 1, 7},   {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4},
                          {11, 10, 2}, {10, 7, 6}, {7, 1, 8},  {3, 9, 4},  {3, 4, 2},   {3, 2, 6}, {3, 6, 8},
                          {3, 8, 9},  {4, 9, 5},  {2, 4, 11}, {6, 2, 10}, {8, 6, 7},   {9, 8, 1}};
    for (auto& tr : f) tri(v[tr[0]], v[tr[1]], v[tr[2]], color);
}

void MeshBuilder::append(const MeshData& other, const mat4& m) {
    const auto base = static_cast<uint32_t>(data_.vertices.size());
    const glm::mat3 nm = glm::transpose(glm::inverse(glm::mat3(m)));
    for (const auto& v : other.vertices)
        data_.vertices.push_back({vec3(m * vec4(v.pos, 1.0f)), glm::normalize(nm * v.normal), v.color});
    for (auto i : other.indices) data_.indices.push_back(base + i);
}

MeshData make_ground_mesh(float half, int cells) {
    MeshBuilder b;
    const float step = 2.0f * half / float(cells);
    for (int z = 0; z < cells; ++z)
        for (int x = 0; x < cells; ++x) {
            const float x0 = -half + float(x) * step, z0 = -half + float(z) * step;
            const bool alt = ((x + z) & 1) != 0;
            const uint32_t c = alt ? rgba(96, 168, 72) : rgba(84, 152, 64);
            b.quad({x0, 0, z0}, {x0, 0, z0 + step}, {x0 + step, 0, z0 + step}, {x0 + step, 0, z0}, c);
        }
    return b.take();
}

MeshData make_tree_mesh() {
    MeshBuilder b;
    b.box({0, 0.6f, 0}, {0.15f, 0.6f, 0.15f}, rgba(110, 72, 40));
    b.cone({0, 1.0f, 0}, 1.1f, 1.6f, 6, rgba(40, 120, 56));
    b.cone({0, 2.0f, 0}, 0.8f, 1.3f, 6, rgba(52, 140, 64));
    return b.take();
}

} // namespace gk::render
