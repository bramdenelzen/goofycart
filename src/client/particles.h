#pragma once
// Client-only particles: sparks, flames, smoke, dust, confetti and glints.
// Pure presentation, simulated per rendered frame and drawn as one batch.
#include "render/renderer.h"

#include <vector>

namespace gk {

struct Particle {
    vec3 pos{0.0f}, vel{0.0f};
    float life = 1.0f, age = 0.0f;  // seconds
    float size0 = 0.2f, size1 = 0.2f;
    vec4 color0{1.0f}, color1{1.0f, 1.0f, 1.0f, 0.0f};
    float gravity = 0.0f;           // m/s^2 downward
    float drag = 0.0f;              // per second
    float stretch = 0.0f;           // streak length per m/s
    bool additive = false;
};

class Particles {
public:
    void emit(const Particle& p) {
        if (list_.size() < kMax) list_.push_back(p);
    }
    void update(float dt);
    void draw(render::Renderer& r) const;
    void clear() { list_.clear(); }
    size_t size() const { return list_.size(); }

    // A small random number generator for emitters.
    float rnd() {
        seed_ ^= seed_ << 13;
        seed_ ^= seed_ >> 17;
        seed_ ^= seed_ << 5;
        return float(seed_ & 0xffffff) / float(0xffffff);
    }
    vec3 rnd_dir() { return glm::normalize(vec3(rnd() - 0.5f, rnd() - 0.5f, rnd() - 0.5f) + vec3(1e-4f)); }

private:
    static constexpr size_t kMax = 4000;
    std::vector<Particle> list_;
    uint32_t seed_ = 0x9e3779b9u;
};

} // namespace gk
