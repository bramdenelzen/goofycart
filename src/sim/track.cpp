#include "gk/sim/track.h"

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/CollisionDispatch.h>
#include <Jolt/Physics/Collision/PhysicsMaterialSimple.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>
#include <stdexcept>

namespace gk::sim {

namespace {
void init_jolt_once() {
    static std::once_flag once;
    std::call_once(once, [] {
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
    });
}
JPH::Vec3 to_jph(vec3 v) { return JPH::Vec3(v.x, v.y, v.z); }
vec3 from_jph(JPH::Vec3 v) { return vec3(v.GetX(), v.GetY(), v.GetZ()); }
} // namespace

// Jolt is used only for queries: the static track is one MeshShape (with its
// own bounding volume tree) and karts probe it with rays and spheres.
class TrackCollision {
public:
    explicit TrackCollision(const Track& t) {
        init_jolt_once();
        JPH::VertexList verts;
        verts.reserve(t.vertices.size());
        for (const auto& v : t.vertices) verts.push_back(JPH::Float3(v.x, v.y, v.z));
        JPH::IndexedTriangleList tris;
        tris.reserve(t.triangles.size());
        for (const auto& tr : t.triangles) tris.push_back(JPH::IndexedTriangle(tr.a, tr.b, tr.c, uint32_t(tr.surface)));
        JPH::PhysicsMaterialList materials;
        for (int i = 0; i < int(Surface::Count); ++i) {
            materials.push_back(new JPH::PhysicsMaterialSimple(surface_name(Surface(i)), JPH::Color::sGrey));
        }
        materials_ = materials;
        JPH::MeshShapeSettings settings(std::move(verts), std::move(tris), std::move(materials));
        settings.mActiveEdgeCosThresholdAngle = 0.996195f; // 5 degrees
        JPH::ShapeSettings::ShapeResult r = settings.Create();
        if (r.HasError()) throw std::runtime_error(std::string("track mesh: ") + r.GetError().c_str());
        mesh_ = r.Get();
    }

    Surface surface_of(const JPH::SubShapeID& id) const {
        const JPH::PhysicsMaterial* m = mesh_->GetMaterial(id);
        for (size_t i = 0; i < materials_.size(); ++i)
            if (materials_[i].GetPtr() == m) return Surface(i);
        return Surface::Road;
    }

    std::optional<RayHit> raycast(vec3 origin, vec3 dir, float max_dist) const {
        JPH::RayCast ray(to_jph(origin), to_jph(dir * max_dist));
        JPH::RayCastResult hit;
        if (!mesh_->CastRay(ray, JPH::SubShapeIDCreator(), hit)) return std::nullopt;
        RayHit out;
        out.distance = hit.mFraction * max_dist;
        out.point = origin + dir * out.distance;
        out.normal = from_jph(mesh_->GetSurfaceNormal(hit.mSubShapeID2, to_jph(out.point)));
        out.surface = surface_of(hit.mSubShapeID2);
        return out;
    }

    void collide_sphere(vec3 c, float radius, std::vector<WallContact>& out) const {
        JPH::SphereShape sphere(radius);
        sphere.SetEmbedded();
        JPH::CollideShapeSettings settings;
        settings.mBackFaceMode = JPH::EBackFaceMode::IgnoreBackFaces;
        settings.mActiveEdgeMode = JPH::EActiveEdgeMode::CollideOnlyWithActive;
        JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
        JPH::CollisionDispatch::sCollideShapeVsShape(&sphere, mesh_.GetPtr(), JPH::Vec3::sOne(), JPH::Vec3::sOne(),
                                                     JPH::Mat44::sTranslation(to_jph(c)), JPH::Mat44::sIdentity(),
                                                     JPH::SubShapeIDCreator(), JPH::SubShapeIDCreator(), settings,
                                                     collector);
        for (const auto& h : collector.mHits) {
            const JPH::Vec3 axis = h.mPenetrationAxis;
            if (axis.LengthSq() < 1e-12f) continue;
            out.push_back({-from_jph(axis.Normalized()), h.mPenetrationDepth, surface_of(h.mSubShapeID2)});
        }
    }

private:
    JPH::RefConst<JPH::Shape> mesh_;
    JPH::PhysicsMaterialList materials_;
};

Track::Track() = default;
Track::~Track() = default;
Track::Track(Track&&) noexcept = default;
Track& Track::operator=(Track&&) noexcept = default;

void build_racing_line(Track& track); // racing_line.cpp

void Track::finalize() {
    collision_ = std::make_unique<TrackCollision>(*this);
    build_racing_line(*this);
}

int Track::wrap(int sample) const {
    const int n = int(center.size());
    if (n == 0) return 0;
    if (closed) return ((sample % n) + n) % n;
    return std::clamp(sample, 0, n - 1);
}

vec3 Track::line_point(int sample) const {
    const CenterSample& c = center[size_t(wrap(sample))];
    return line.empty() ? c.pos : c.pos + c.right * line[size_t(wrap(sample))].offset;
}

vec3 Track::mover_pos(size_t i, double tick) const {
    const Mover& m = movers[i];
    const double t = tick / double(std::max(m.period, 1)) + double(m.phase);
    return m.base + m.travel * float(std::sin(t * 2.0 * 3.14159265358979));
}

float Track::mover_top(const Mover& m, float x, float z) const {
    if (std::abs(x) > m.half.x || std::abs(z) > m.half.z) return -1.0f;
    const float h = 2.0f * m.half.y;
    if (m.kind == Mover::Kind::Platform && m.ramp > 0.0f) {
        const float in = m.half.z - std::abs(z);
        if (in < m.ramp) return h * in / m.ramp;
    }
    return h;
}

std::optional<RayHit> Track::raycast(vec3 origin, vec3 dir, float max_dist) const {
    std::optional<RayHit> best = collision_ ? collision_->raycast(origin, dir, max_dist) : std::nullopt;
    for (size_t i = 0; i < movers.size(); ++i) {
        const Mover& m = movers[i];
        const float down = -glm::dot(dir, m.up);
        if (down < 0.5f) continue; // only the wheel probes look for floors here
        const vec3 rel = origin - mover_pos(i, mover_time);
        const float x = glm::dot(rel, m.right), y = glm::dot(rel, m.up), z = glm::dot(rel, m.forward);
        const float top = mover_top(m, x, z);
        if (top < 0.0f || y < top - 0.3f) continue; // off it, or starting inside it
        const float d = (y - top) / down;
        if (d < 0.0f || d > max_dist || (best && best->distance <= d)) continue;
        RayHit hit;
        hit.distance = d;
        hit.point = origin + dir * d;
        hit.surface = m.kind == Mover::Kind::Platform ? m.deck : Surface::Road;
        hit.normal = m.up;
        if (m.kind == Mover::Kind::Platform && m.ramp > 0.0f && m.half.z - std::abs(z) < m.ramp)
            hit.normal = glm::normalize(m.up + m.forward * ((z > 0.0f ? 1.0f : -1.0f) * 2.0f * m.half.y / m.ramp));
        best = hit;
    }
    return best;
}

void Track::collide_sphere(vec3 c, float radius, std::vector<WallContact>& out) const {
    if (collision_) collision_->collide_sphere(c, radius, out);
    for (size_t i = 0; i < movers.size(); ++i) {
        const Mover& m = movers[i];
        const vec3 rel = c - mover_pos(i, mover_time);
        const vec3 l(glm::dot(rel, m.right), glm::dot(rel, m.up), glm::dot(rel, m.forward));
        // The solid under the sphere: a box, its height following the ramps.
        const float zc = std::clamp(l.z, -m.half.z, m.half.z);
        const float top = mover_top(m, std::clamp(l.x, -m.half.x, m.half.x), zc);
        if (top <= 0.05f) continue;
        const vec3 q(std::clamp(l.x, -m.half.x, m.half.x), std::clamp(l.y, 0.0f, top), zc);
        vec3 d = l - q;
        float dist = glm::length(d);
        if (dist >= radius) continue;
        vec3 n_local;
        float depth;
        if (dist > 1e-4f) {
            n_local = d / dist;
            depth = radius - dist;
        } else { // centre inside: out through the nearest side
            const float px = m.half.x - std::abs(l.x), pz = m.half.z - std::abs(l.z), py = top - l.y;
            if (py < px && py < pz) n_local = {0, 1, 0}, depth = py + radius;
            else if (px < pz) n_local = {l.x > 0.0f ? 1.0f : -1.0f, 0, 0}, depth = px + radius;
            else n_local = {0, 0, l.z > 0.0f ? 1.0f : -1.0f}, depth = pz + radius;
        }
        const vec3 n = glm::normalize(m.right * n_local.x + m.up * n_local.y + m.forward * n_local.z);
        out.push_back({n, depth, Surface::Wall});
    }
}

int Track::nearest_sample(vec3 p, int hint) const {
    const int n = int(center.size());
    if (n == 0) return 0;
    auto d2 = [&](int i) {
        const vec3 d = center[size_t(i)].pos - p;
        return glm::dot(d, d);
    };
    int best = 0;
    float best_d = std::numeric_limits<float>::max();
    if (hint >= 0 && hint < n) {
        constexpr int kWindow = 40; // 80 m either side
        for (int k = -kWindow; k <= kWindow; ++k) {
            const int i = ((hint + k) % n + n) % n;
            const float d = d2(i);
            if (d < best_d) best_d = d, best = i;
        }
        if (best_d < 30.0f * 30.0f) return best;
    }
    for (int i = 0; i < n; ++i) {
        const float d = d2(i);
        if (d < best_d) best_d = d, best = i;
    }
    return best;
}

int Track::checkpoint_of(int sample) const {
    int cp = 0;
    for (int i = 0; i < int(checkpoints.size()); ++i)
        if (sample >= checkpoints[size_t(i)]) cp = i;
    return cp;
}

Track::Slot Track::grid_slot(int index) const {
    const int row = index / 2, col = index % 2;
    const float behind = 8.0f + float(row) * 6.0f + (col ? 3.0f : 0.0f);
    float s = length - behind;
    int i = int(center.size()) - 1;
    while (i > 0 && center[size_t(i)].s > s) --i;
    const CenterSample& c = center[size_t(i)];
    const float lateral = (col == 0 ? -1.0f : 1.0f) * c.half_width * 0.4f;
    return {c.pos + c.right * lateral + c.up * 0.05f, c.tangent};
}

int Track::respawn_sample_before(int sample) const {
    const int n = int(center.size());
    for (int k = 0; k < n; ++k) {
        const int i = ((sample - k) % n + n) % n;
        if (center[size_t(i)].respawn_ok && center[size_t(i)].has_floor) return i;
    }
    return 0;
}

} // namespace gk::sim
