#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace gk {

using vec2 = glm::vec2;
using vec3 = glm::vec3;
using vec4 = glm::vec4;
using quat = glm::quat;
using mat4 = glm::mat4;

inline constexpr float kPi = 3.14159265358979323846f;

// World convention: +Y up, right-handed, metres.
inline constexpr vec3 kUp{0.0f, 1.0f, 0.0f};

template <class T>
constexpr T clamp01(T v) { return v < T(0) ? T(0) : (v > T(1) ? T(1) : v); }

inline float approach(float current, float target, float max_delta) {
    if (current < target) return current + max_delta > target ? target : current + max_delta;
    return current - max_delta < target ? target : current - max_delta;
}

} // namespace gk
