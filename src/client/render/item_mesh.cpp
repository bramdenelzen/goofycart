#include "item_mesh.h"

#include <glm/gtx/quaternion.hpp>

#include <cmath>

namespace gk::render {

MeshData make_item_box_mesh() {
    // A cube with a different colour on each face pair and a "?" block on top.
    MeshBuilder b;
    const float h = 0.55f;
    const vec3 p[8] = {{-h, -h, -h}, {h, -h, -h}, {h, h, -h}, {-h, h, -h}, {-h, -h, h}, {h, -h, h}, {h, h, h}, {-h, h, h}};
    const uint32_t c0 = rgba(255, 90, 200), c1 = rgba(90, 200, 255), c2 = rgba(255, 220, 60);
    b.quad(p[1], p[0], p[3], p[2], c0);  // -Z
    b.quad(p[4], p[5], p[6], p[7], c0);  // +Z
    b.quad(p[0], p[4], p[7], p[3], c1);  // -X
    b.quad(p[5], p[1], p[2], p[6], c1);  // +X
    b.quad(p[3], p[7], p[6], p[2], c2);  // +Y
    b.quad(p[0], p[1], p[5], p[4], c2);  // -Y
    // "?" mark on two sides, built from small blocks.
    const uint32_t w = rgba(255, 255, 255);
    for (float z : {-h - 0.01f, h + 0.01f}) {
        b.box({0.0f, 0.25f, z}, {0.2f, 0.06f, 0.02f}, w);
        b.box({0.16f, 0.12f, z}, {0.06f, 0.1f, 0.02f}, w);
        b.box({0.0f, 0.0f, z}, {0.06f, 0.06f, 0.02f}, w);
        b.box({0.0f, -0.25f, z}, {0.06f, 0.06f, 0.02f}, w);
    }
    return b.take();
}

MeshData make_banana_mesh() {
    // Three yellow segments in an arc, brown tips.
    MeshBuilder b;
    const uint32_t y = rgba(250, 220, 50), br = rgba(110, 80, 40);
    for (int i = 0; i < 3; ++i) {
        const float a = (float(i) - 1.0f) * 0.55f;
        const vec3 c(std::sin(a) * 0.35f, 0.25f + std::cos(a) * 0.12f, 0.0f);
        MeshBuilder seg;
        seg.box({0, 0, 0}, {0.13f, 0.1f, 0.1f}, y);
        b.append(seg.data(), glm::rotate(glm::translate(mat4(1.0f), c), -a, vec3(0, 0, 1)));
    }
    b.box({-0.52f, 0.33f, 0.0f}, {0.04f, 0.04f, 0.04f}, br);
    b.box({0.52f, 0.33f, 0.0f}, {0.04f, 0.04f, 0.04f}, br);
    return b.take();
}

MeshData make_shell_mesh(uint32_t color, bool spikes) {
    MeshBuilder b;
    // Dome, white rim and a dark underside.
    MeshBuilder dome;
    dome.icosphere({0, 0, 0}, 0.38f, color);
    b.append(dome.data(), glm::scale(glm::translate(mat4(1.0f), vec3(0, 0.08f, 0)), vec3(1.0f, 0.75f, 1.0f)));
    const int n = 8;
    for (int i = 0; i < n; ++i) {
        const float a0 = 2.0f * kPi * float(i) / float(n), a1 = 2.0f * kPi * float(i + 1) / float(n);
        const vec3 o0(std::cos(a0) * 0.42f, 0.0f, std::sin(a0) * 0.42f), o1(std::cos(a1) * 0.42f, 0.0f, std::sin(a1) * 0.42f);
        const vec3 i0 = o0 * 0.7f, i1 = o1 * 0.7f;
        b.quad(i0 + vec3(0, 0.02f, 0), i1 + vec3(0, 0.02f, 0), o1, o0, rgba(250, 250, 240));
        b.tri(vec3(0, -0.12f, 0), o0, o1, rgba(240, 230, 200));
    }
    if (spikes) {
        for (int i = 0; i < 6; ++i) {
            const float a = 2.0f * kPi * float(i) / 6.0f;
            MeshBuilder s;
            s.cone({0, 0, 0}, 0.07f, 0.22f, 4, rgba(250, 250, 250));
            const vec3 dir = glm::normalize(vec3(std::cos(a), 0.9f, std::sin(a)));
            const vec3 base = vec3(0, 0.08f, 0) + dir * 0.3f;
            const quat q = glm::rotation(vec3(0, 1, 0), dir);
            b.append(s.data(), glm::translate(mat4(1.0f), base) * glm::mat4_cast(q));
        }
        // Little wings
        b.box({0.45f, 0.25f, 0.0f}, {0.15f, 0.02f, 0.08f}, rgba(250, 250, 250));
        b.box({-0.45f, 0.25f, 0.0f}, {0.15f, 0.02f, 0.08f}, rgba(250, 250, 250));
    }
    return b.take();
}

MeshData make_bomb_mesh() {
    MeshBuilder b;
    b.icosphere({0, 0.4f, 0}, 0.42f, rgba(30, 30, 40));
    b.box({0, 0.86f, 0}, {0.08f, 0.06f, 0.08f}, rgba(160, 160, 170));
    b.box({0, 0.98f, 0}, {0.02f, 0.06f, 0.02f}, rgba(220, 200, 150));
    b.box({0.18f, 0.5f, 0.38f}, {0.05f, 0.08f, 0.02f}, rgba(255, 255, 255)); // eyes
    b.box({-0.18f, 0.5f, 0.38f}, {0.05f, 0.08f, 0.02f}, rgba(255, 255, 255));
    b.box({0.3f, 0.05f, 0.0f}, {0.1f, 0.05f, 0.14f}, rgba(240, 180, 40));     // feet
    b.box({-0.3f, 0.05f, 0.0f}, {0.1f, 0.05f, 0.14f}, rgba(240, 180, 40));
    return b.take();
}

MeshData make_ball_mesh(uint32_t color) {
    MeshBuilder b;
    b.icosphere({0, 0, 0}, 0.3f, color);
    return b.take();
}

MeshData make_boomerang_mesh() {
    MeshBuilder b;
    const uint32_t c = rgba(90, 150, 255), t = rgba(255, 200, 60);
    MeshBuilder arm;
    arm.box({0.3f, 0, 0}, {0.3f, 0.04f, 0.08f}, c);
    arm.box({0.6f, 0, 0}, {0.06f, 0.05f, 0.09f}, t);
    b.append(arm.data(), glm::rotate(mat4(1.0f), 0.6f, vec3(0, 1, 0)));
    b.append(arm.data(), glm::rotate(mat4(1.0f), kPi - 0.6f, vec3(0, 1, 0)));
    return b.take();
}

MeshData make_bullet_mesh() {
    // Black body with a rounded nose along +Z, white eyes and fins.
    MeshBuilder b;
    const uint32_t body = rgba(35, 35, 45), white = rgba(240, 240, 240);
    const int sides = 8;
    const float r = 0.85f;
    for (int i = 0; i < sides; ++i) {
        const float a0 = 2.0f * kPi * float(i) / float(sides), a1 = 2.0f * kPi * float(i + 1) / float(sides);
        const vec3 d0(std::cos(a0) * r, std::sin(a0) * r + 0.6f, 0), d1(std::cos(a1) * r, std::sin(a1) * r + 0.6f, 0);
        const vec3 back(0, 0, -1.4f), front(0, 0, 0.9f);
        b.quad(d1 + back, d0 + back, d0 + front, d1 + front, body);
        const vec3 nose(0, 0.6f, 1.9f);
        b.tri(d0 + front, nose, d1 + front, body);
        b.tri(d0 + back, d1 + back, vec3(0, 0.6f, -1.4f), rgba(90, 90, 100));
    }
    b.box({0.35f, 1.05f, 1.2f}, {0.12f, 0.16f, 0.05f}, white);
    b.box({-0.35f, 1.05f, 1.2f}, {0.12f, 0.16f, 0.05f}, white);
    b.box({0.0f, 1.6f, -1.1f}, {0.05f, 0.3f, 0.3f}, white);
    b.box({0.95f, 0.6f, -1.1f}, {0.3f, 0.05f, 0.3f}, white);
    b.box({-0.95f, 0.6f, -1.1f}, {0.3f, 0.05f, 0.3f}, white);
    return b.take();
}

MeshData make_chomper_mesh() {
    // Green stalk, red head with white spots and teeth, opening towards +Z.
    MeshBuilder b;
    const uint32_t g = rgba(60, 170, 60), red = rgba(220, 40, 40), w = rgba(250, 250, 250);
    b.box({0, 0.25f, 0}, {0.06f, 0.25f, 0.06f}, g);
    b.box({0, 0.62f, 0.08f}, {0.26f, 0.12f, 0.26f}, red); // upper jaw
    b.box({0, 0.42f, 0.08f}, {0.24f, 0.06f, 0.24f}, red); // lower jaw
    b.box({0.12f, 0.76f, 0.1f}, {0.06f, 0.02f, 0.06f}, w);
    b.box({-0.12f, 0.76f, -0.02f}, {0.05f, 0.02f, 0.05f}, w);
    for (float x : {-0.15f, 0.0f, 0.15f}) b.box({x, 0.5f, 0.32f}, {0.03f, 0.04f, 0.02f}, w);
    b.box({0.18f, 0.12f, 0}, {0.12f, 0.02f, 0.05f}, g); // leaves
    b.box({-0.18f, 0.12f, 0}, {0.12f, 0.02f, 0.05f}, g);
    return b.take();
}

} // namespace gk::render
