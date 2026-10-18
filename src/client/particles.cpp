#include "particles.h"

#include <algorithm>
#include <cmath>

namespace gk {

void Particles::update(float dt) {
    for (Particle& p : list_) {
        p.age += dt;
        p.vel.y -= p.gravity * dt;
        p.vel *= std::exp(-p.drag * dt);
        p.pos += p.vel * dt;
    }
    std::erase_if(list_, [](const Particle& p) { return p.age >= p.life; });
}

void Particles::draw(render::Renderer& r) const {
    for (const Particle& p : list_) {
        const float t = std::clamp(p.age / std::max(p.life, 1e-3f), 0.0f, 1.0f);
        r.particle(p.pos, glm::mix(p.size0, p.size1, t), glm::mix(p.color0, p.color1, t), p.additive, p.vel, p.stretch);
    }
}

} // namespace gk
