#include "mesh.h"

#include "textures.h"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace gk::render {

vec2 MeshBuilder::project(vec3 p, vec3 n) const {
    const vec3 a = glm::abs(n);
    const vec2 uv = a.y >= a.x && a.y >= a.z ? vec2(p.x, p.z) : a.x >= a.z ? vec2(p.z, -p.y) : vec2(p.x, -p.y);
    return uv / tile_;
}

void MeshBuilder::tri(vec3 a, vec3 b, vec3 c, uint32_t color) {
    vec3 n = glm::cross(b - a, c - a);
    const float len = glm::length(n);
    n = len > 1e-12f ? n / len : kUp;
    const auto base = static_cast<uint32_t>(data_.vertices.size());
    for (const vec3& p : {a, b, c}) data_.vertices.push_back({p, n, color, project(p, n), layer_});
    data_.indices.insert(data_.indices.end(), {base, base + 1, base + 2});
}

void MeshBuilder::tri_uv(vec3 a, vec3 b, vec3 c, vec2 ua, vec2 ub, vec2 uc, uint32_t color) {
    vec3 n = glm::cross(b - a, c - a);
    const float len = glm::length(n);
    n = len > 1e-12f ? n / len : kUp;
    const auto base = static_cast<uint32_t>(data_.vertices.size());
    data_.vertices.push_back({a, n, color, ua, layer_});
    data_.vertices.push_back({b, n, color, ub, layer_});
    data_.vertices.push_back({c, n, color, uc, layer_});
    data_.indices.insert(data_.indices.end(), {base, base + 1, base + 2});
}

void MeshBuilder::quad(vec3 a, vec3 b, vec3 c, vec3 d, uint32_t color) {
    vec3 n = glm::cross(b - a, c - a);
    const float len = glm::length(n);
    n = len > 1e-12f ? n / len : kUp;
    quad_uv(a, b, c, d, project(a, n), project(b, n), project(c, n), project(d, n), color);
}

void MeshBuilder::quad_uv(vec3 a, vec3 b, vec3 c, vec3 d, vec2 ua, vec2 ub, vec2 uc, vec2 ud, uint32_t color) {
    vec3 n = glm::cross(b - a, c - a);
    const float len = glm::length(n);
    n = len > 1e-12f ? n / len : kUp;
    const auto base = static_cast<uint32_t>(data_.vertices.size());
    data_.vertices.push_back({a, n, color, ua, layer_});
    data_.vertices.push_back({b, n, color, ub, layer_});
    data_.vertices.push_back({c, n, color, uc, layer_});
    data_.vertices.push_back({d, n, color, ud, layer_});
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
    // Split each face in four once (80 faces): round enough at this resolution.
    for (auto& tr : f) {
        const vec3 a = v[tr[0]], b = v[tr[1]], d = v[tr[2]];
        auto mid = [&](vec3 p, vec3 q) { return c + glm::normalize((p + q) * 0.5f - c) * r; };
        const vec3 ab = mid(a, b), bd = mid(b, d), da = mid(d, a);
        tri(a, ab, da, color);
        tri(ab, b, bd, color);
        tri(da, bd, d, color);
        tri(ab, bd, da, color);
    }
}

// ---- smooth surfaces

void MeshBuilder::grid(const std::vector<vec3>& pts, int rows, int cols, bool wrap, uint32_t color, float u_tiles, float v_tiles) {
    if (rows < 2 || cols < 2 || int(pts.size()) < rows * cols) return;
    const int stride = cols; // vertices per row
    std::vector<vec3> normals(size_t(rows * cols), vec3(0.0f));
    auto at = [&](int r, int c) { return pts[size_t(r * stride + (c % cols))]; };
    auto idx = [&](int r, int c) { return size_t(r * stride + (c % cols)); };
    const int quads_across = wrap ? cols : cols - 1;
    // Face normals accumulated on their corners; degenerate faces add nothing.
    for (int r = 0; r + 1 < rows; ++r)
        for (int c = 0; c < quads_across; ++c) {
            const vec3 a = at(r, c), b = at(r, c + 1), d = at(r + 1, c + 1), e = at(r + 1, c);
            const vec3 n = glm::cross(b - a, d - a) + glm::cross(d - a, e - a);
            normals[idx(r, c)] += n;
            normals[idx(r, c + 1)] += n;
            normals[idx(r + 1, c + 1)] += n;
            normals[idx(r + 1, c)] += n;
        }
    // Poles and seams: a ring collapsed to a point shares one normal.
    for (int r = 0; r < rows; ++r) {
        bool collapsed = true;
        for (int c = 1; c < cols && collapsed; ++c) collapsed = glm::distance(at(r, c), at(r, 0)) < 1e-5f;
        if (!collapsed) continue;
        vec3 sum(0.0f);
        for (int c = 0; c < cols; ++c) sum += normals[idx(r, c)];
        for (int c = 0; c < cols; ++c) normals[idx(r, c)] = sum;
    }
    const auto base = static_cast<uint32_t>(data_.vertices.size());
    // With wrap the seam column is emitted twice so the texture can tile.
    const int out_cols = wrap ? cols + 1 : cols;
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < out_cols; ++c) {
            vec3 n = normals[idx(r, c)];
            const float len = glm::length(n);
            n = len > 1e-12f ? n / len : kUp;
            const vec2 uv(u_tiles * float(c) / float(out_cols - 1), v_tiles * float(r) / float(rows - 1));
            data_.vertices.push_back({at(r, c), n, color, uv, layer_});
        }
    auto vi = [&](int r, int c) { return base + uint32_t(r * out_cols + c); };
    for (int r = 0; r + 1 < rows; ++r)
        for (int c = 0; c + 1 < out_cols; ++c) {
            const vec3 a = at(r, c), b = at(r, c + 1), d = at(r + 1, c + 1), e = at(r + 1, c);
            // Skip the degenerate triangle at a pole, keep the other.
            if (glm::distance(a, b) > 1e-6f) data_.indices.insert(data_.indices.end(), {vi(r, c), vi(r, c + 1), vi(r + 1, c + 1)});
            if (glm::distance(d, e) > 1e-6f) data_.indices.insert(data_.indices.end(), {vi(r, c), vi(r + 1, c + 1), vi(r + 1, c)});
        }
}

void MeshBuilder::sphere(vec3 c, vec3 radii, uint32_t color, int segments, int rings) {
    std::vector<vec3> pts;
    pts.reserve(size_t((rings + 1) * segments));
    for (int r = 0; r <= rings; ++r) {
        const float phi = kPi * float(r) / float(rings);
        for (int s = 0; s < segments; ++s) {
            const float th = 2.0f * kPi * float(s) / float(segments);
            pts.push_back(c + radii * vec3(std::sin(phi) * std::cos(th), std::cos(phi), std::sin(phi) * std::sin(th)));
        }
    }
    grid(pts, rings + 1, segments, true, color, 2.0f, 1.0f);
}

namespace {
void basis_for(vec3 axis, vec3& u, vec3& v) {
    const vec3 a = glm::normalize(axis);
    const vec3 ref = std::abs(a.y) < 0.9f ? vec3(0, 1, 0) : vec3(1, 0, 0);
    u = glm::normalize(glm::cross(ref, a));
    v = glm::cross(a, u);
}
} // namespace

void MeshBuilder::lathe(vec3 origin, vec3 axis, const std::vector<vec2>& profile, int segments, uint32_t color, float u_tiles,
                        float v_tiles) {
    if (profile.size() < 2) return;
    vec3 u, v;
    basis_for(axis, u, v);
    const vec3 a = glm::normalize(axis);
    std::vector<vec3> pts;
    pts.reserve(profile.size() * size_t(segments));
    for (const vec2& p : profile)
        for (int s = 0; s < segments; ++s) {
            const float th = 2.0f * kPi * float(s) / float(segments);
            pts.push_back(origin + a * p.y + (u * std::cos(th) + v * std::sin(th)) * p.x);
        }
    grid(pts, int(profile.size()), segments, true, color, u_tiles, v_tiles);
}

void MeshBuilder::loft(const std::vector<std::vector<vec3>>& rings, uint32_t color, bool cap_ends) {
    if (rings.size() < 2 || rings[0].size() < 3) return;
    const int cols = int(rings[0].size());
    std::vector<vec3> pts;
    std::vector<std::vector<vec3>> all = rings;
    if (cap_ends) {
        auto centre = [](const std::vector<vec3>& ring) {
            vec3 c(0.0f);
            for (const vec3& p : ring) c += p;
            return c / float(ring.size());
        };
        // Pull the end rings together, which the grid turns into a fan with a
        // crease-free normal: a slightly domed end rather than a sharp lid.
        const vec3 c0 = centre(rings.front()), c1 = centre(rings.back());
        all.insert(all.begin(), std::vector<vec3>(size_t(cols), c0));
        all.push_back(std::vector<vec3>(size_t(cols), c1));
    }
    for (const auto& ring : all) {
        if (int(ring.size()) != cols) return;
        pts.insert(pts.end(), ring.begin(), ring.end());
    }
    grid(pts, int(all.size()), cols, true, color, 1.0f, 1.0f);
}

void MeshBuilder::capsule(vec3 a, vec3 b, float r, uint32_t color, int segments, int rings) {
    const float len = glm::length(b - a);
    std::vector<vec2> prof;
    for (int i = 0; i <= rings; ++i) { // cap around a, from its far pole up to the seam
        const float t = kPi * 0.5f * float(i) / float(rings);
        prof.push_back({r * std::sin(t), -r * std::cos(t)});
    }
    for (int i = rings; i >= 0; --i) { // cap around b
        const float t = kPi * 0.5f * float(i) / float(rings);
        prof.push_back({r * std::sin(t), len + r * std::cos(t)});
    }
    lathe(a, len > 1e-6f ? b - a : vec3(0, 1, 0), prof, segments, color);
}

void MeshBuilder::tube(vec3 a, vec3 b, float ra, float rb, uint32_t color, int segments, bool caps) {
    const float len = glm::length(b - a);
    std::vector<vec2> prof;
    if (caps) prof.push_back({0.0f, 0.0f});
    prof.push_back({ra, 0.0f});
    prof.push_back({rb, len});
    if (caps) prof.push_back({0.0f, len});
    lathe(a, len > 1e-6f ? b - a : vec3(0, 1, 0), prof, segments, color);
}

void MeshBuilder::rounded_box(vec3 c, vec3 half, float r, uint32_t color, int segments, int rings) {
    r = std::min({r, half.x, half.y, half.z});
    const vec3 inner = half - vec3(r);
    std::vector<vec3> pts;
    for (int ri = 0; ri <= rings; ++ri) {
        const float phi = kPi * float(ri) / float(rings);
        for (int s = 0; s < segments; ++s) {
            const float th = 2.0f * kPi * float(s) / float(segments);
            const vec3 n(std::sin(phi) * std::cos(th), std::cos(phi), std::sin(phi) * std::sin(th));
            // Support point of the inner box in direction n, pushed out by the
            // radius: the box's Minkowski sum with a sphere. The sign is
            // softened so the flat faces get triangles too.
            const vec3 soft = glm::clamp(n * 4.0f, vec3(-1.0f), vec3(1.0f));
            pts.push_back(c + inner * soft + n * r);
        }
    }
    grid(pts, rings + 1, segments, true, color, 2.0f, 1.0f);
}

void MeshBuilder::torus(vec3 c, vec3 axis, float R, float r, uint32_t color, int segments, int sides) {
    vec3 u, v;
    basis_for(axis, u, v);
    const vec3 a = glm::normalize(axis);
    std::vector<vec3> pts;
    for (int i = 0; i <= sides; ++i) {
        const float ph = 2.0f * kPi * float(i) / float(sides);
        for (int s = 0; s < segments; ++s) {
            const float th = 2.0f * kPi * float(s) / float(segments);
            const vec3 radial = u * std::cos(th) + v * std::sin(th);
            pts.push_back(c + radial * (R + r * std::cos(ph)) + a * (r * std::sin(ph)));
        }
    }
    grid(pts, sides + 1, segments, true, color, 4.0f, 1.0f);
}

void MeshBuilder::sweep(const std::vector<vec3>& path, const std::vector<float>& radii, uint32_t color, int segments, bool caps) {
    if (path.size() < 2 || radii.size() != path.size()) return;
    std::vector<std::vector<vec3>> rings;
    vec3 u, v;
    vec3 prev_t = glm::normalize(path[1] - path[0]);
    basis_for(prev_t, u, v);
    for (size_t i = 0; i < path.size(); ++i) {
        vec3 t = i + 1 < path.size() ? path[i + 1] - path[i] : path[i] - path[i - 1];
        if (i > 0 && i + 1 < path.size()) t = glm::normalize(path[i + 1] - path[i]) + glm::normalize(path[i] - path[i - 1]);
        if (glm::length(t) < 1e-6f) t = prev_t;
        t = glm::normalize(t);
        // Parallel transport: rotate the frame from the previous tangent to this one.
        const vec3 axis = glm::cross(prev_t, t);
        if (glm::length(axis) > 1e-6f) {
            const quat q = glm::angleAxis(std::acos(std::clamp(glm::dot(prev_t, t), -1.0f, 1.0f)), glm::normalize(axis));
            u = q * u;
            v = q * v;
        }
        prev_t = t;
        std::vector<vec3> ring;
        for (int s = 0; s < segments; ++s) {
            const float th = 2.0f * kPi * float(s) / float(segments);
            ring.push_back(path[i] + (u * std::cos(th) + v * std::sin(th)) * radii[i]);
        }
        rings.push_back(std::move(ring));
    }
    loft(rings, color, caps);
}

void MeshBuilder::append(const MeshData& other, const mat4& m) {
    const auto base = static_cast<uint32_t>(data_.vertices.size());
    const glm::mat3 nm = glm::transpose(glm::inverse(glm::mat3(m)));
    for (const auto& v : other.vertices)
        data_.vertices.push_back({vec3(m * vec4(v.pos, 1.0f)), glm::normalize(nm * v.normal), v.color, v.uv, v.layer});
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
    b.texture(TexBark, 1.0f);
    b.lathe({0, 0, 0}, {0, 1, 0}, {{0.0f, 0.0f}, {0.22f, 0.0f}, {0.16f, 0.6f}, {0.13f, 1.4f}, {0.0f, 1.5f}}, 12, rgba(110, 72, 40), 2.0f, 1.0f);
    b.texture(TexLeaves, 1.0f);
    // Three bulging tiers of foliage, each a lathe with a soft underside.
    struct Tier { float y, r, h; uint32_t c; };
    for (const Tier& t : {Tier{0.9f, 1.15f, 1.5f, rgba(40, 120, 56)}, Tier{1.9f, 0.85f, 1.3f, rgba(52, 140, 64)}, Tier{2.8f, 0.55f, 1.0f, rgba(60, 152, 70)}}) {
        std::vector<vec2> prof{{0.0f, 0.0f}, {t.r * 0.7f, 0.05f}, {t.r, 0.25f * t.h}, {t.r * 0.8f, 0.55f * t.h}, {t.r * 0.45f, 0.82f * t.h}, {0.0f, t.h}};
        b.lathe({0, t.y, 0}, {0, 1, 0}, prof, 14, t.c, 3.0f, 1.0f);
    }
    return b.take();
}

} // namespace gk::render
