#include "seam_check.h"

#include "track_mesh.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_map>

namespace gk::render {
namespace {

struct Tri {
    vec3 a, b, c;
    uint32_t ia = 0, ib = 0, ic = 0; // sim vertex ids (unused for render triangles)
};

// Height of the triangle's plane at (x, z) if (x, z) is inside it seen from above.
bool height_in(const Tri& t, float x, float z, float& y) {
    const vec2 p(x, z), a(t.a.x, t.a.z), b(t.b.x, t.b.z), c(t.c.x, t.c.z);
    const float d = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
    if (std::abs(d) < 1e-8f) return false;
    const float l0 = ((b.y - c.y) * (p.x - c.x) + (c.x - b.x) * (p.y - c.y)) / d;
    const float l1 = ((c.y - a.y) * (p.x - c.x) + (a.x - c.x) * (p.y - c.y)) / d;
    const float l2 = 1.0f - l0 - l1;
    constexpr float e = -1e-4f;
    if (l0 < e || l1 < e || l2 < e) return false;
    y = l0 * t.a.y + l1 * t.b.y + l2 * t.c.y;
    return true;
}

// Triangles binned by the 4 m cells their footprint touches.
class Grid {
public:
    static constexpr float kCell = 4.0f;
    void add(const Tri& t) {
        const int id = int(tris.size());
        tris.push_back(t);
        const float x0 = std::min({t.a.x, t.b.x, t.c.x}), x1 = std::max({t.a.x, t.b.x, t.c.x});
        const float z0 = std::min({t.a.z, t.b.z, t.c.z}), z1 = std::max({t.a.z, t.b.z, t.c.z});
        for (int i = cell(x0); i <= cell(x1); ++i)
            for (int j = cell(z0); j <= cell(z1); ++j) bins[key(i, j)].push_back(id);
    }
    template <class F> void each_at(float x, float z, F f) const {
        auto it = bins.find(key(cell(x), cell(z)));
        if (it == bins.end()) return;
        for (int id : it->second) {
            float y;
            if (height_in(tris[size_t(id)], x, z, y)) f(id, y);
        }
    }
    std::vector<Tri> tris;

private:
    static int cell(float v) { return int(std::floor(v / kCell)); }
    static int64_t key(int i, int j) { return (int64_t(i) << 32) ^ int64_t(uint32_t(j)); }
    std::unordered_map<int64_t, std::vector<int>> bins;
};

bool is_floor(const sim::Track& t, const sim::TrackTriangle& tri) {
    if (tri.surface == sim::Surface::Wall || tri.surface == sim::Surface::OutOfBounds) return false;
    const vec3 n = glm::cross(t.vertices[tri.b] - t.vertices[tri.a], t.vertices[tri.c] - t.vertices[tri.a]);
    return glm::length(n) > 1e-6f && std::abs(glm::normalize(n).y) > 0.3f;
}

// Props that are meant to be off the ground, or stand on walls and other props.
bool airborne_prop(const std::string& name) {
    for (const char* p : {"cloud", "lantern", "balloon", "bird", "bunting", "blimp", "gull", "kite", "banner", "string", "lamp", "sign", "flag",
                          "billboard", "neon", "building", "star", "mesa",
                          "rock", "boulder"}) // (rocks are placed by their middle, half sunk)
        if (name.find(p) != std::string::npos) return true;
    return false;
}

} // namespace

std::vector<SeamIssue> find_seams(const sim::Track& t) {
    std::vector<SeamIssue> raw;
    auto add = [&](const char* kind, vec3 p, float size, std::string what) {
        raw.push_back({kind, p, t.nearest_sample(p), size, 1, std::move(what)});
    };

    // ---- the sim's floors: cracks, steps and overlaps between them
    Grid floors;
    std::vector<int> floor_of_tri(t.triangles.size(), -1);
    for (size_t i = 0; i < t.triangles.size(); ++i) {
        const auto& tri = t.triangles[i];
        if (!is_floor(t, tri)) continue;
        floor_of_tri[i] = int(floors.tris.size());
        floors.add({t.vertices[tri.a], t.vertices[tri.b], t.vertices[tri.c], tri.a, tri.b, tri.c});
    }
    auto shares = [](const Tri& x, uint32_t a, uint32_t b) {
        for (uint32_t v : {x.ia, x.ib, x.ic})
            if (v == a || v == b) return true;
        return false;
    };
    // Edges used by one floor triangle only, and edges a wall stands on.
    std::map<std::pair<uint32_t, uint32_t>, std::pair<int, int>> edges; // -> (uses, floor tri)
    std::map<std::pair<uint32_t, uint32_t>, bool> walled;
    auto ek = [](uint32_t a, uint32_t b) { return std::pair(std::min(a, b), std::max(a, b)); };
    for (size_t i = 0; i < t.triangles.size(); ++i) {
        const auto& tri = t.triangles[i];
        const uint32_t v[3] = {tri.a, tri.b, tri.c};
        for (int k = 0; k < 3; ++k) {
            const auto key = ek(v[k], v[(k + 1) % 3]);
            if (tri.surface == sim::Surface::Wall) walled[key] = true;
            else if (floor_of_tri[i] >= 0) {
                auto& e = edges[key];
                ++e.first;
                e.second = floor_of_tri[i];
            }
        }
    }
    for (const auto& [key, use] : edges) {
        if (use.first != 1 || walled.count(key)) continue;
        const vec3 a = t.vertices[key.first], b = t.vertices[key.second];
        const Tri& own = floors.tris[size_t(use.second)];
        const vec3 third = (own.ia != key.first && own.ia != key.second) ? own.a : (own.ib != key.first && own.ib != key.second) ? own.b : own.c;
        const vec3 m = 0.5f * (a + b);
        vec3 out = glm::cross(b - a, kUp);
        out.y = 0.0f;
        if (glm::length(out) < 1e-5f) continue;
        out = glm::normalize(out);
        if (glm::dot(out, third - m) > 0.0f) out = -out;
        // What floor is just across this edge, and how far out.
        auto across = [&](float d, float& y) {
            bool found = false;
            float best = 1e9f;
            const vec3 p = m + out * d;
            floors.each_at(p.x, p.z, [&](int id, float h) {
                if (shares(floors.tris[size_t(id)], key.first, key.second) || id == use.second) return;
                if (std::abs(h - m.y) < 0.6f && std::abs(h - m.y) < std::abs(best - m.y)) best = h, found = true;
            });
            y = best;
            return found;
        };
        float y;
        if (across(0.02f, y)) {
            if (std::abs(y - m.y) >= 0.03f) add("step", m, std::abs(y - m.y), "floors meet at different heights");
            continue;
        }
        for (float d : {0.05f, 0.1f, 0.2f, 0.35f, 0.6f})
            if (across(d, y)) {
                add("crack", m, d, "open gap between two floors");
                break;
            }
    }
    for (size_t i = 0; i < floors.tris.size(); ++i) {
        const Tri& f = floors.tris[i];
        const vec3 c = (f.a + f.b + f.c) / 3.0f;
        floors.each_at(c.x, c.z, [&](int id, float h) {
            if (size_t(id) <= i) return; // each pair once
            const Tri& g = floors.tris[size_t(id)];
            if (shares(g, f.ia, f.ib) || shares(g, f.ic, f.ic)) return;
            const float dy = std::abs(h - c.y);
            if (dy < 0.1f) add("overlap", c, dy, "two floors on top of each other (z-fighting)");
        });
    }

    // ---- terrain through the road
    const auto terrain = make_terrain_sampler(t);
    if (t.ground)
        for (const Tri& f : floors.tris)
            for (const vec3& p : {f.a, f.b, f.c, (f.a + f.b + f.c) / 3.0f}) {
                const float h = terrain(p.x, p.z);
                if (std::isfinite(h) && h - p.y > 0.03f) add("terrain", p, h - p.y, "ground pokes up through the road");
            }

    // ---- props: standing on something?
    std::vector<PropPlacement> props;
    const MeshData scenery = make_scenery_mesh(t, &props);
    const MeshData road = make_track_mesh(t, false, -1);
    Grid solid;
    for (const MeshData* m : {&scenery, &road})
        for (size_t k = 0; k + 2 < m->indices.size(); k += 3) {
            const vec3 a = m->vertices[m->indices[k]].pos, b = m->vertices[m->indices[k + 1]].pos, c = m->vertices[m->indices[k + 2]].pos;
            const vec3 n = glm::cross(b - a, c - a);
            if (glm::length(n) < 1e-6f || std::abs(glm::normalize(n).y) < 0.2f) continue;
            solid.add({a, b, c});
        }
    for (const PropPlacement& p : props) {
        if (airborne_prop(p.name)) continue;
        const vec3 base(p.transform * vec4(0.0f, 0.0f, 0.0f, 1.0f));
        // Only by the road, where it shows (far off, props stand on cliffs and
        // rocks that are props themselves).
        const sim::CenterSample& near = t.sample(t.nearest_sample(base));
        if (glm::length(vec2(base.x - near.pos.x, base.z - near.pos.z)) > near.half_width + 14.0f) continue;
        float support = -1e9f;
        // A grandstand stands on its corners (gobby_props.cpp), other props on their origin.
        std::vector<vec3> feet{base};
        if (p.name == "grandstand")
            for (float x : {-2.8f, 2.8f})
                for (float z : {0.0f, -1.8f}) feet.push_back(vec3(p.transform * vec4(x, 0.0f, z, 1.0f)));
        for (const vec3& f : feet)
            solid.each_at(f.x, f.z, [&](int, float h) {
                if (h <= f.y + 0.3f && h >= f.y - 0.3f) support = std::max(support, h + (base.y - f.y));
                else if (h <= base.y + 0.3f) support = std::max(support, h);
            });
        if (t.ground) {
            const float h = terrain(base.x, base.z);
            if (std::isfinite(h) && h <= base.y + 0.3f) support = std::max(support, h);
        }
        const float gap = base.y - support;
        if (gap > 0.3f) add("floating", base, std::min(gap, 99.0f), p.name + (support < -1e8f ? " over nothing" : " above the ground"));
    }

    // ---- bridges: room under them?
    for (size_t i = 0; i < t.road.size(); i += 2) {
        const sim::CenterSample& lo = t.road[i];
        if (!lo.has_floor) continue;
        for (size_t j = 0; j < t.road.size(); j += 2) {
            const sim::CenterSample& hi = t.road[j];
            if (!hi.has_floor || (lo.branch == hi.branch && std::abs(lo.s - hi.s) < 40.0f)) continue;
            const float dy = hi.pos.y - lo.pos.y;
            if (dy < 0.4f || dy > 3.6f) continue;
            if (glm::length(vec2(hi.pos.x - lo.pos.x, hi.pos.z - lo.pos.z)) < 0.8f * (lo.half_width + hi.half_width))
                add("clearance", 0.5f * (lo.pos + hi.pos), dy, "a road passes this close over another");
        }
    }

    // ---- merge findings of a kind within a few metres
    std::stable_sort(raw.begin(), raw.end(), [](const SeamIssue& a, const SeamIssue& b) { return a.kind < b.kind; });
    std::vector<SeamIssue> out;
    for (const SeamIssue& r : raw) {
        bool merged = false;
        for (auto it = out.rbegin(); it != out.rend() && it->kind == r.kind; ++it)
            if (glm::distance(it->pos, r.pos) < 6.0f) {
                if (r.size > it->size) it->size = r.size, it->pos = r.pos, it->sample = r.sample, it->what = r.what;
                ++it->count;
                merged = true;
                break;
            }
        if (!merged) out.push_back(r);
    }
    return out;
}

} // namespace gk::render
