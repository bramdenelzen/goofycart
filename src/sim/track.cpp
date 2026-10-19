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
    // The triangles of one group (0 the fixed track, k a branch that can vanish).
    TrackCollision(const Track& t, uint8_t group) {
        init_jolt_once();
        JPH::VertexList verts;
        verts.reserve(t.vertices.size());
        for (const auto& v : t.vertices) verts.push_back(JPH::Float3(v.x, v.y, v.z));
        JPH::IndexedTriangleList tris;
        tris.reserve(t.triangles.size());
        for (const auto& tr : t.triangles)
            if (tr.group == group) tris.push_back(JPH::IndexedTriangle(tr.a, tr.b, tr.c, uint32_t(tr.surface)));
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
// (collision_ holds one TrackCollision per triangle group.)
Track::~Track() = default;
Track::Track(Track&&) noexcept = default;
Track& Track::operator=(Track&&) noexcept = default;

void build_racing_line(Track& track); // racing_line.cpp

void Track::finalize() {
    if (road.size() < center.size()) road = center; // hand-built tracks (tests): no branches
    collision_.clear();
    uint8_t groups = 0;
    for (const auto& tr : triangles) groups = std::max(groups, tr.group);
    for (uint8_t g = 0; g <= groups; ++g) {
        bool any = false;
        for (const auto& tr : triangles) any |= tr.group == g;
        collision_.push_back(any ? std::make_unique<TrackCollision>(*this, g) : nullptr);
    }
    // The CPUs' line is drawn on the road as built: every road there, no
    // zones or barriers (they come and go; the CPUs react to them as they drive).
    auto zones_ = std::move(zones);
    auto barriers_ = std::move(barriers);
    auto gates_ = std::move(gate_state);
    auto busy_ = std::move(branch_busy);
    zones.clear(), barriers.clear(), gate_state.clear(), branch_busy.clear();
    build_racing_line(*this);
    zones = std::move(zones_), barriers = std::move(barriers_), gate_state = std::move(gates_), branch_busy = std::move(busy_);
}

// A group's triangles count while it's there: group k is branch k - 1's road.
bool Track::group_present(size_t g) const {
    if (g == 0) return true;
    const size_t b = g - 1;
    return b >= branches.size() || branch_present(int(b));
}

int Track::next(int i, uint32_t route) const {
    const int n = int(center.size());
    if (i < n) {
        for (size_t b = 0; b < branches.size() && b < 32; ++b)
            if (branches[b].fork == i && (route >> b & 1u) && branch_open(int(b))) return branches[b].first;
        return wrap(i + 1);
    }
    for (const Branch& br : branches)
        if (i >= br.first && i < br.first + br.count) return i + 1 < br.first + br.count ? i + 1 : wrap(br.merge);
    return wrap(i + 1);
}

int Track::prev(int i) const {
    const int n = int(center.size());
    if (i < n) return wrap(i - 1);
    for (const Branch& br : branches)
        if (i >= br.first && i < br.first + br.count) return i > br.first ? i - 1 : br.fork;
    return wrap(i - 1);
}

int Track::advance(int i, int steps, uint32_t route) const {
    for (int k = 0; k < steps; ++k) i = next(i, route);
    for (int k = 0; k > steps; --k) i = prev(i);
    return i;
}

int Track::main_sample(int i) const {
    const int n = int(center.size());
    if (i < n || n == 0) return i;
    const float step = length / float(closed ? n : std::max(n - 1, 1));
    return wrap(int(std::round(road[size_t(i)].s / std::max(step, 1e-3f))));
}

std::vector<Track::Strip> Track::strips() const {
    std::vector<Strip> out{{0, int(center.size()), 0, closed, -1}};
    for (size_t b = 0; b < branches.size(); ++b)
        out.push_back({branches[b].first, branches[b].count, branches[b].first_vertex, false, int(b)});
    return out;
}

bool Track::in_deep_water(vec3 p, int sample) const {
    for (const Zone& z : zones) {
        if (!z.deep || sample < z.first || sample >= z.first + z.count || !gate_holds(z.gate)) continue;
        const CenterSample& c = road[size_t(sample)];
        const float lat = glm::dot(p - c.pos, c.right);
        if (lat >= z.lo - 1.0f && lat <= z.hi + 1.0f && p.y < z.level) return true;
    }
    return false;
}

vec3 Track::current_at(vec3 p, int sample, bool underwater, bool grounded) const {
    vec3 carry(0.0f);
    for (const Current& cu : currents) {
        if (cu.dry ? (underwater || !grounded) : !underwater) continue;
        if (sample < cu.first || sample >= cu.first + cu.count || !gate_holds(cu.gate)) continue;
        const CenterSample& c = road[size_t(sample)];
        const float lat = glm::dot(p - c.pos, c.right);
        if (lat < cu.lo || lat > cu.hi) continue;
        carry += c.tangent * cu.push + c.right * cu.drift;
    }
    return carry;
}

bool Track::vent_active(const Vent& v, double tick) const {
    if (!gate_holds(v.gate)) return false;
    if (v.period <= 0) return true;
    const double t = std::fmod(tick + double(v.phase) * double(v.period), double(v.period));
    return t < double(v.open);
}

const Track::Tunnel* Track::tunnel_at(int sample) const {
    for (const Tunnel& t : tunnels)
        if (sample >= t.first && sample < t.first + t.count) return &t;
    return nullptr;
}

std::optional<float> Track::tunnel_roof(vec3 p, int sample) const {
    const Tunnel* t = tunnel_at(sample);
    if (!t) return std::nullopt;
    const CenterSample& c = road[size_t(sample)];
    return c.pos.y + t->roof_at(sample - t->first, glm::dot(p - c.pos, c.right));
}

std::optional<Surface> Track::zone_surface(vec3 p, int sample) const {
    for (const Zone& z : zones) {
        if (z.deep || sample < z.first || sample >= z.first + z.count || !gate_holds(z.gate)) continue;
        const CenterSample& c = road[size_t(sample)];
        const float lat = glm::dot(p - c.pos, c.right);
        if (lat >= z.lo && lat <= z.hi && std::abs(glm::dot(p - c.pos, c.up)) < 2.5f) return z.surface;
    }
    return std::nullopt;
}

int Track::wrap(int sample) const { // on the main loop
    const int n = int(center.size());
    if (n == 0) return 0;
    if (closed) return ((sample % n) + n) % n;
    return std::clamp(sample, 0, n - 1);
}

int Track::section_at(float s) const {
    int section = 1;
    for (const float b : section_s) section += s >= b;
    return section;
}

vec3 Track::line_point(int sample) const {
    const CenterSample& c = road[size_t(sample)];
    return line.size() <= size_t(sample) ? c.pos : c.pos + c.right * line[size_t(sample)].offset;
}

// A press rests up for the first half of its period, slams down in a tenth
// of it, sits for a fifth, and rises over the last fifth.
float Track::press_phase(const Mover& m, double tick) const {
    const double t = std::fmod(tick / double(std::max(m.period, 1)) + double(m.phase), 1.0);
    const double f = t < 0.0 ? t + 1.0 : t;
    if (f < 0.5) return 0.0f;
    if (f < 0.6) { const float x = float((f - 0.5) / 0.1); return x * x; } // picking up speed
    if (f < 0.8) return 1.0f;
    return 1.0f - float((f - 0.8) / 0.2);
}
bool Track::press_falling(const Mover& m, double tick) const {
    const double t = std::fmod(tick / double(std::max(m.period, 1)) + double(m.phase), 1.0);
    const double f = t < 0.0 ? t + 1.0 : t;
    return f >= 0.5 && f < 0.8;
}

Track::Frame Track::mover_frame(size_t i, double tick) const {
    const Mover& m = movers[i];
    if (m.kind != Mover::Kind::Swing) return {m.right, m.up, m.forward};
    const double t = tick / double(std::max(m.period, 1)) + double(m.phase);
    const float a = m.angle * float(std::sin(t * 2.0 * 3.14159265358979));
    const float c = std::cos(a), sn = std::sin(a);
    return {glm::normalize(m.right * c - m.up * sn), glm::normalize(m.up * c + m.right * sn), m.forward};
}

vec3 Track::mover_pos(size_t i, double tick) const {
    const Mover& m = movers[i];
    if (m.kind == Mover::Kind::Press) return m.base + m.travel * press_phase(m, tick);
    if (m.kind == Mover::Kind::Swing) { // hangs `arm` under its pivot, swaying about the road's direction
        const vec3 pivot = m.base + m.up * m.arm;
        return pivot - mover_frame(i, tick).up * m.arm;
    }
    const double t = tick / double(std::max(m.period, 1)) + double(m.phase);
    return m.base + m.travel * float(std::sin(t * 2.0 * 3.14159265358979));
}

float Track::wave_ease(const Wave& w, float d) const {
    const float ease = std::min(10.0f, w.length / 3.0f);
    const float x = std::clamp(std::min(d, w.length - d) / std::max(ease, 1e-3f), 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

const Track::Wave* Track::wave_at(int sample) const {
    for (const Wave& w : waves)
        if (sample >= w.first && sample < w.first + w.count) return &w;
    return nullptr;
}

float Track::wave_height(vec3 p, double tick, float* slope, vec3* tangent) const {
    if (slope) *slope = 0.0f;
    for (const Wave& w : waves) {
        if (!gate_holds(w.gate)) continue;
        // The wave's nearest sample, and where `p` sits along its road.
        int best = -1;
        float best_d = 1e18f;
        for (int i = w.first; i < w.first + w.count; ++i) {
            const vec3 d = road[size_t(i)].pos - p;
            const float dd = glm::dot(d, d);
            if (dd < best_d) best_d = dd, best = i;
        }
        if (best < 0) continue;
        const CenterSample& c = road[size_t(best)];
        const vec3 rel = p - c.pos;
        if (std::abs(glm::dot(rel, c.right)) > c.half_width + 8.0f || std::abs(glm::dot(rel, c.up)) > 6.0f) continue;
        float d = c.u + glm::dot(rel, c.tangent) - w.u0;
        if (closed && c.branch < 0) { // the stretch may run over the start line
            if (d < -0.5f * length) d += length;
            if (d > 0.5f * length) d -= length;
        }
        if (d < -0.5f || d > w.length + 0.5f) continue;
        const float ease = wave_ease(w, std::clamp(d, 0.0f, w.length));
        const float k = 2.0f * kPi / std::max(w.wavelength, 1.0f);
        const float ph = k * d - 2.0f * kPi * float(tick / double(std::max(w.period, 1)) - double(w.phase));
        if (slope) *slope = ease * w.amplitude * k * std::cos(ph);
        if (tangent) *tangent = c.tangent;
        return ease * w.amplitude * std::sin(ph);
    }
    return 0.0f;
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
    std::optional<RayHit> best;
    for (size_t g = 0; g < collision_.size(); ++g) {
        if (!collision_[g] || !group_present(g)) continue;
        const auto hit = collision_[g]->raycast(origin, dir, max_dist);
        if (hit && (!best || hit->distance < best->distance)) best = hit;
    }
    // Zones change the floor's surface while they're on.
    if (best && !zones.empty() && best->surface != Surface::Wall && -glm::dot(dir, best->normal) > 0.5f) {
        const int i = nearest_sample(best->point, -1);
        if (const auto zs = zone_surface(best->point, i)) best->surface = *zs;
    }
    // Barriers: boxes standing on the road while up.
    for (const Barrier& b : barriers) {
        if (!barrier_up(b)) continue;
        // Slab test in the barrier's frame.
        const vec3 o = origin - b.pos;
        const vec3 lo_(-b.half.x, 0.0f, -b.half.z), hi_(b.half.x, 2.0f * b.half.y, b.half.z);
        const vec3 ol(glm::dot(o, b.right), glm::dot(o, b.up), glm::dot(o, b.forward));
        const vec3 dl(glm::dot(dir, b.right), glm::dot(dir, b.up), glm::dot(dir, b.forward));
        float t0 = 0.0f, t1 = max_dist;
        int axis = -1;
        bool miss = false;
        for (int a = 0; a < 3 && !miss; ++a) {
            if (std::abs(dl[a]) < 1e-6f) {
                miss = ol[a] < lo_[a] || ol[a] > hi_[a];
                continue;
            }
            float ta = (lo_[a] - ol[a]) / dl[a], tb = (hi_[a] - ol[a]) / dl[a];
            if (ta > tb) std::swap(ta, tb);
            if (ta > t0) t0 = ta, axis = a;
            t1 = std::min(t1, tb);
            miss = t0 > t1;
        }
        if (miss || axis < 0 || (best && best->distance <= t0)) continue;
        RayHit hit;
        hit.distance = t0;
        hit.point = origin + dir * t0;
        const vec3 axes[3] = {b.right, b.up, b.forward};
        hit.normal = axes[axis] * (dl[axis] > 0.0f ? -1.0f : 1.0f);
        hit.surface = axis == 1 ? Surface::Road : Surface::Wall;
        best = hit;
    }
    for (size_t i = 0; i < movers.size(); ++i) {
        const Mover& m = movers[i];
        const Frame fr = mover_frame(i, mover_time);
        const float down = -glm::dot(dir, fr.up);
        if (down < 0.5f) continue; // only the wheel probes look for floors here
        const vec3 rel = origin - mover_pos(i, mover_time);
        const float x = glm::dot(rel, fr.right), y = glm::dot(rel, fr.up), z = glm::dot(rel, fr.forward);
        const float top = mover_top(m, x, z);
        if (top < 0.0f || y < top - 0.3f) continue; // off it, or starting inside it
        const float d = (y - top) / down;
        if (d < 0.0f || d > max_dist || (best && best->distance <= d)) continue;
        RayHit hit;
        hit.distance = d;
        hit.point = origin + dir * d;
        hit.surface = m.kind == Mover::Kind::Platform || m.kind == Mover::Kind::Swing ? m.deck : Surface::Road;
        hit.normal = fr.up;
        if (m.kind != Mover::Kind::Block && m.kind != Mover::Kind::Press && m.ramp > 0.0f && m.half.z - std::abs(z) < m.ramp)
            hit.normal = glm::normalize(fr.up + fr.forward * ((z > 0.0f ? 1.0f : -1.0f) * 2.0f * m.half.y / m.ramp));
        best = hit;
    }
    // A wavy stretch: its floor is where the wave has lifted it this tick.
    if (best && !waves.empty() && best->surface != Surface::Wall && best->normal.y > 0.5f && dir.y < -0.3f) {
        float slope = 0.0f;
        vec3 tangent(0.0f);
        const float h = wave_height(best->point, mover_time, &slope, &tangent);
        if (h != 0.0f || slope != 0.0f) {
            const float d = best->distance - h / -dir.y;
            if (d > max_dist) return std::nullopt;
            best->distance = std::max(d, -1.0f); // negative: the ray started under the risen floor
            best->point = origin + dir * best->distance;
            best->normal = glm::normalize(best->normal - tangent * slope);
        }
    }
    return best;
}

void Track::collide_sphere(vec3 c, float radius, std::vector<WallContact>& out) const {
    for (size_t g = 0; g < collision_.size(); ++g)
        if (collision_[g] && group_present(g)) collision_[g]->collide_sphere(c, radius, out);
    for (const Barrier& b : barriers) {
        if (!barrier_up(b)) continue;
        const vec3 rel = c - b.pos;
        const vec3 l(glm::dot(rel, b.right), glm::dot(rel, b.up), glm::dot(rel, b.forward));
        const vec3 q(std::clamp(l.x, -b.half.x, b.half.x), std::clamp(l.y, 0.0f, 2.0f * b.half.y), std::clamp(l.z, -b.half.z, b.half.z));
        const vec3 d = l - q;
        const float dist = glm::length(d);
        if (dist >= radius) continue;
        vec3 n_local;
        float depth;
        if (dist > 1e-4f) {
            n_local = d / dist, depth = radius - dist;
        } else { // inside: out along the barrier's thin direction, towards whichever side is nearer
            n_local = {0, 0, l.z > 0.0f ? 1.0f : -1.0f};
            depth = b.half.z - std::abs(l.z) + radius;
        }
        out.push_back({glm::normalize(b.right * n_local.x + b.up * n_local.y + b.forward * n_local.z), depth, Surface::Wall});
    }
    for (size_t i = 0; i < movers.size(); ++i) {
        const Mover& m = movers[i];
        const Frame fr = mover_frame(i, mover_time);
        const vec3 rel = c - mover_pos(i, mover_time);
        const vec3 l(glm::dot(rel, fr.right), glm::dot(rel, fr.up), glm::dot(rel, fr.forward));
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
        const vec3 n = glm::normalize(fr.right * n_local.x + fr.up * n_local.y + fr.forward * n_local.z);
        out.push_back({n, depth, Surface::Wall});
    }
}

int Track::nearest_sample(vec3 p, int hint) const {
    const int n = int(road.size()), nm = int(center.size());
    if (n == 0) return 0;
    auto d2 = [&](int i) {
        const vec3 d = road[size_t(i)].pos - p;
        return glm::dot(d, d);
    };
    // How far outside a sample's road `p` is (negative: how deep inside).
    auto outside = [&](int i) { return std::sqrt(d2(i)) - road[size_t(i)].half_width; };
    // The nearest main-loop sample, then a side road's if `p` is further
    // inside that road than the main one: where they run side by side, a
    // kart on the main road's lanes stays on the main road.
    auto pick = [&](int main_best, bool windowed, float s0) {
        int best_b = -1;
        float best_bd = std::numeric_limits<float>::max();
        const float total = std::max(length, 1.0f);
        for (int i = nm; i < n; ++i) {
            if (windowed) {
                float ds = std::abs(road[size_t(i)].s - s0);
                if (closed) ds = std::min(ds, total - ds);
                if (ds > 80.0f) continue;
            }
            const float d = d2(i);
            if (d < best_bd) best_bd = d, best_b = i;
        }
        if (best_b < 0) return main_best;
        // A shut side road only claims a kart that's clearly left the main road.
        const int b = road[size_t(best_b)].branch;
        if (!branch_open(b) && outside(main_best) < 1.0f) return main_best;
        return outside(best_b) < outside(main_best) ? best_b : main_best;
    };
    int best = 0;
    float best_d = std::numeric_limits<float>::max();
    if (hint >= 0 && hint < n) {
        // Near the hint along the race (80 m either side): keeps a kart on
        // its own deck where the track crosses itself.
        const int h = main_sample(hint);
        constexpr int kWindow = 40; // 80 m either side
        for (int k = -kWindow; k <= kWindow; ++k) {
            if (!closed && (h + k < 0 || h + k >= nm)) continue; // an open road has no other side
            const int i = ((h + k) % nm + nm) % nm;
            const float d = d2(i);
            if (d < best_d) best_d = d, best = i;
        }
        const int chosen = pick(best, true, road[size_t(hint)].s);
        if (d2(chosen) < 30.0f * 30.0f || best_d < 30.0f * 30.0f) return chosen;
    }
    best_d = std::numeric_limits<float>::max();
    for (int i = 0; i < nm; ++i) {
        const float d = d2(i);
        if (d < best_d) best_d = d, best = i;
    }
    return pick(best, false, 0.0f);
}

int Track::checkpoint_of(int sample) const {
    sample = main_sample(sample);
    int cp = 0;
    for (int i = 0; i < int(checkpoints.size()); ++i)
        if (sample >= checkpoints[size_t(i)]) cp = i;
    return cp;
}

Track::Slot Track::grid_slot(int index) const {
    const int row = index / 2, col = index % 2;
    const float behind = 8.0f + float(row) * 6.0f + (col ? 3.0f : 0.0f);
    // Behind the start line: the end of a loop, or before a point-to-point road's line.
    const float s = (point_to_point ? center[size_t(std::clamp(start, 0, int(center.size()) - 1))].s : length) - behind;
    int i = int(center.size()) - 1;
    while (i > 0 && center[size_t(i)].s > s) --i;
    const CenterSample& c = center[size_t(i)];
    const float lateral = (col == 0 ? -1.0f : 1.0f) * c.half_width * 0.4f;
    return {c.pos + c.right * lateral + c.up * 0.05f, c.tangent};
}

int Track::respawn_sample_before(int sample) const {
    const int n = int(road.size());
    if (n == 0) return 0;
    if (sample < 0 || sample >= n) sample = 0;
    auto there = [&](int i) { // a branch that has vanished is no place to put a kart
        const int b = road[size_t(i)].branch;
        return b < 0 || branch_open(b);
    };
    auto safe = [&](int i) { // not into lava that's on, nor just before it
        for (int k = 0, j = i; k < 12; ++k, j = next(j)) {
            const auto z = zone_surface(road[size_t(j)].pos, j);
            if (z && *z == Surface::OutOfBounds) return false;
        }
        return true;
    };
    for (int k = 0, i = sample; k < n; ++k, i = prev(i))
        if (road[size_t(i)].respawn_ok && road[size_t(i)].has_floor && there(i) && safe(i)) return i;
    return 0;
}

} // namespace gk::sim
