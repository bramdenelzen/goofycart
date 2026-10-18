// The painted pieces along a track: walls (with panel variety, hazard
// stripes, chevrons on the outside of sharp corners and sponsor boards),
// the rail and posts on top of them, kerbs and pennant flags. One painted
// texture per track, in the theme's colours, low on polygons.
#include "trackside.h"

#include "gobby_util.h"
#include "track_mesh.h"

namespace gk::render {
namespace {

using namespace gobby;
constexpr int kStride = 9; // vertices per centre sample (track_builder.cpp)

vec4 rgba_vec(uint32_t c) { return vec4(float(c & 0xff), float((c >> 8) & 0xff), float((c >> 16) & 0xff), 255.0f) / 255.0f; }

float smooth01(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

std::pair<std::vector<bool>, std::vector<bool>> walls_of(const sim::Track& t) {
    const int n = int(t.center.size());
    std::vector<bool> left(size_t(n), false), right(size_t(n), false);
    for (const auto& tri : t.triangles) {
        if (tri.surface != sim::Surface::Wall) continue;
        for (uint32_t vi : {tri.a, tri.b, tri.c}) {
            if (vi >= uint32_t(n * kStride)) break;
            const int col = int(vi % kStride), sample = int(vi / kStride);
            if (col == 7) left[size_t(sample)] = true;
            if (col == 8) right[size_t(sample)] = true;
        }
    }
    return {left, right};
}

// Texture regions (8 x 8 grid on 1024 px).
const UvRect r_wall_plain = span(0, 0, 4, 2, 8), r_wall_stripe = span(4, 0, 8, 2, 8), r_wall_chevron = span(0, 2, 4, 4, 8),
             r_wall_sponsor = span(4, 2, 8, 4, 8), r_wall_back = span(0, 4, 2, 5, 8), r_kerb = span(2, 4, 4, 5, 8),
             r_rail = span(4, 4, 5, 5, 8), r_post = span(5, 4, 6, 5, 8), r_pole = span(6, 4, 7, 5, 8), r_finial = span(7, 4, 8, 5, 8),
             r_flag[3] = {span(0, 5, 2, 7, 8), span(2, 5, 4, 7, 8), span(4, 5, 6, 7, 8)};

// A wall panel's frame: top and bottom trims, a bevel and rivets.
void panel_frame(Canvas& t, const UvRect& r, vec4 trim) {
    t.band_v(r, 0.0f, 0.1f, trim, 0.0f);
    t.band_v(r, 0.1f, 0.13f, vec4(1.0f, 1.0f, 1.0f, 0.35f), 0.0f);
    t.band_v(r, 0.9f, 1.0f, shade(trim, 0.7f), 0.0f);
    t.band_v(r, 0.82f, 0.9f, vec4(0.0f, 0.0f, 0.0f, 0.18f), 0.06f); // grime at the foot
    t.band_u(r, 0.0f, 0.012f, vec4(0.0f, 0.0f, 0.0f, 0.35f), 0.0f); // seam between panels
    t.band_u(r, 0.012f, 0.022f, vec4(1.0f, 1.0f, 1.0f, 0.25f), 0.0f);
    for (float u : {0.04f, 0.5f, 0.96f})
        for (float v : {0.18f, 0.76f}) {
            t.ellipse(r, {u, v}, {0.008f, 0.02f}, vec4(0.0f, 0.0f, 0.0f, 0.4f), 0.4f);
            t.ellipse(r, {u - 0.002f, v - 0.005f}, {0.006f, 0.014f}, vec4(1.0f, 1.0f, 1.0f, 0.5f), 0.4f);
        }
}

void paint_atlas(Canvas& t, const ThemeStyle& th) {
    const vec4 a = rgba_vec(th.wall_a), b = rgba_vec(th.wall_b);
    // Plain: two raised panels in the light colour.
    t.gradient_v(r_wall_plain, shade(a, 1.05f), shade(a, 0.86f));
    t.noise(r_wall_plain, 0.12f, 8.0f, 3, a);
    t.rounded(r_wall_plain, {0.04f, 0.2f, 0.47f, 0.78f}, 0.04f, vec4(1.0f, 1.0f, 1.0f, 0.1f));
    t.rounded(r_wall_plain, {0.53f, 0.2f, 0.96f, 0.78f}, 0.04f, vec4(1.0f, 1.0f, 1.0f, 0.1f));
    panel_frame(t, r_wall_plain, b);
    // Hazard stripes.
    t.custom(r_wall_stripe, [&](float u, float v) { return ((int(std::floor((u * 2.0f - v * 0.5f) * 4.0f + 100.0f)) & 1) ? b : a); });
    t.gradient_v(r_wall_stripe, vec4(1.0f, 1.0f, 1.0f, 0.12f), vec4(0.0f, 0.0f, 0.0f, 0.2f));
    panel_frame(t, r_wall_stripe, shade(b, 0.8f));
    // Chevrons pointing along +u: the way to turn.
    const vec4 chev_bg = col(30, 30, 36), chev = col(255, 210, 40);
    t.fill(r_wall_chevron, chev_bg);
    t.custom(r_wall_chevron, [&](float u, float v) {
        const float x = std::fmod(u * 3.0f, 1.0f), y = std::abs(v - 0.5f) * 2.0f;
        const float d = std::abs(x - 0.65f + 0.35f * y); // the point at the middle, furthest along +u
        return vec4(chev.r, chev.g, chev.b, 1.0f - smooth01((d - 0.12f) / 0.02f + 0.5f));
    });
    panel_frame(t, r_wall_chevron, b);
    // Sponsor board: the game's own star badge between speed stripes.
    t.gradient_v(r_wall_sponsor, shade(b, 1.1f), shade(b, 0.8f));
    for (int k = 0; k < 3; ++k) t.band_v(r_wall_sponsor, 0.3f + 0.12f * float(k), 0.35f + 0.12f * float(k), vec4(1.0f, 1.0f, 1.0f, 0.5f), 0.01f);
    t.ellipse(r_wall_sponsor, {0.5f, 0.48f}, {0.13f, 0.34f}, col(255, 255, 255), 0.02f);
    t.star(r_wall_sponsor, {0.5f, 0.48f}, 0.1f, col(250, 196, 40));
    t.ellipse(r_wall_sponsor, {0.22f, 0.48f}, {0.05f, 0.13f}, col(250, 196, 40), 0.03f);
    t.ellipse(r_wall_sponsor, {0.78f, 0.48f}, {0.05f, 0.13f}, col(250, 196, 40), 0.03f);
    panel_frame(t, r_wall_sponsor, shade(a, 0.9f));
    // Back of the walls.
    t.gradient_v(r_wall_back, shade(a, 0.8f), shade(a, 0.55f));
    t.noise(r_wall_back, 0.15f, 6.0f, 5, a);
    // Kerb: a red block then a white block, bevelled.
    t.custom(r_kerb, [](float u, float v) {
        const vec3 c = u < 0.5f ? vec3(0.86f, 0.18f, 0.18f) : vec3(0.95f, 0.95f, 0.93f);
        const float bevel = 1.0f + 0.15f * (1.0f - smooth01(v / 0.2f)) - 0.2f * smooth01((v - 0.8f) / 0.2f);
        const float seam = std::min(std::abs(u - 0.5f), std::min(u, 1.0f - u));
        return vec4(c * bevel * (seam < 0.01f ? 0.7f : 1.0f), 1.0f);
    });
    t.noise(r_kerb, 0.1f, 10.0f, 7, col(200, 200, 200));
    // Metal for the rail, posts, poles; a gold finial.
    t.gradient_u(r_rail, col(235, 238, 245), col(120, 125, 140));
    t.band_u(r_rail, 0.15f, 0.25f, vec4(1.0f, 1.0f, 1.0f, 0.6f), 0.05f);
    t.gradient_u(r_post, shade(b, 1.1f), shade(b, 0.6f));
    t.gradient_u(r_pole, col(230, 232, 238), col(130, 134, 148));
    shade_round(t, r_finial, col(250, 200, 60), 1.2f);
    // Three pennant designs: theme colour with a star, a stripe, a chequer.
    const vec4 designs[3] = {b, a, col(250, 200, 60)};
    for (int k = 0; k < 3; ++k) {
        const UvRect& r = r_flag[k];
        t.fill(r, designs[k]);
        if (k == 0) t.star(r, {0.35f, 0.5f}, 0.16f, col(255, 255, 255));
        if (k == 1) t.band_v(r, 0.38f, 0.62f, b, 0.01f);
        if (k == 2) t.checker(r, 6, vec4(0.0f, 0.0f, 0.0f, 0.0f), vec4(0.0f, 0.0f, 0.0f, 0.25f));
        t.band_u(r, 0.0f, 0.06f, shade(designs[k], 0.6f), 0.0f); // the hem on the pole
        t.vignette(r, 0.2f, 0.08f);
    }
}

} // namespace

PaintedModel make_trackside(const sim::Track& t) {
    PaintedModel m;
    m.texture = Canvas(1024, col(128, 128, 128));
    const ThemeStyle& th = theme_style(t.theme);
    paint_atlas(m.texture, th);
    MeshBuilder b;
    const int n = int(t.center.size());
    if (n < 4) return m;
    const auto [left, right] = walls_of(t);
    auto V = [&](int sample, int col) { return t.vertices[size_t((sample % n) * kStride + col)]; };
    auto uvq = [&](const UvRect& r, float u0, float u1, vec2 out[4]) { // bottom-left, bottom-right, top-right, top-left
        out[0] = r.at(u0, 1.0f);
        out[1] = r.at(u1, 1.0f);
        out[2] = r.at(u1, 0.0f);
        out[3] = r.at(u0, 0.0f);
    };

    // ---- walls: two samples (4 m) per painted panel
    for (int side = 0; side < 2; ++side) {
        const std::vector<bool>& has = side ? right : left;
        const int base = side ? 6 : 0, top = side ? 8 : 7;
        for (int i = 0; i < n; ++i) {
            const int j = (i + 1) % n;
            if (!has[size_t(i)] || !has[size_t(j)]) continue;
            const int panel = i / 2;
            const float u0 = float(i % 2) * 0.5f, u1 = u0 + 0.5f;
            // Outside of a sharp corner: chevrons.
            const sim::CenterSample& c = t.center[size_t(i)];
            const vec3 ta = t.center[size_t((i - 4 + n) % n)].tangent, tb = t.center[size_t((i + 4) % n)].tangent;
            const float turn = glm::dot(tb - ta, c.right); // > 0 turning right
            const bool sharp = glm::dot(ta, tb) < 0.96f;
            const bool outside = sharp && ((turn > 0.0f && side == 0) || (turn < 0.0f && side == 1));
            const UvRect& r = outside ? r_wall_chevron : panel % 9 == 4 ? r_wall_sponsor : panel % 5 == 2 ? r_wall_stripe : r_wall_plain;
            vec2 uv[4];
            uvq(r, u0, u1, uv);
            const vec3 bi = V(i, base), bj = V(j, base), ti = V(i, top), tj = V(j, top);
            if (side == 0) {
                b.quad_uv(bi, bj, tj, ti, uv[0], uv[1], uv[2], uv[3], kWhite); // faces the road
                vec2 bk[4];
                uvq(r_wall_back, u0, u1, bk);
                b.quad_uv(bj, bi, ti, tj, bk[1], bk[0], bk[3], bk[2], kWhite);
            } else {
                b.quad_uv(bj, bi, ti, tj, uv[1], uv[0], uv[3], uv[2], kWhite);
                vec2 bk[4];
                uvq(r_wall_back, u0, u1, bk);
                b.quad_uv(bi, bj, tj, ti, bk[0], bk[1], bk[2], bk[3], kWhite);
            }
        }
        // Rail along the top, a post every five samples.
        int origin = 0;
        while (origin < n && has[size_t(origin)]) ++origin;
        auto flush = [&](int from, int to) {
            std::vector<vec3> path;
            for (int k = from;; k = (k + 1) % n) {
                path.push_back(V(k, top) + t.center[size_t(k)].up * 0.1f);
                if (k == to) break;
            }
            if (path.size() >= 2) {
                region(b, r_rail);
                b.sweep(path, 0.15f, kWhite, 6, true);
            }
            region(b, r_post);
            for (int k = from, count = 0;; k = (k + 1) % n, ++count) {
                if (count % 5 == 0) {
                    const vec3 out = side ? t.center[size_t(k)].right : -t.center[size_t(k)].right;
                    b.tube(V(k, base) + out * 0.1f, V(k, top) + out * 0.1f + t.center[size_t(k)].up * 0.25f, 0.12f, 0.1f, kWhite, 8, true);
                }
                if (k == to) break;
            }
            b.uv_region_off();
        };
        if (origin == n) flush(0, n - 1);
        else {
            int start = -1;
            for (int step = 0; step < n; ++step) {
                const int k = (origin + step) % n;
                if (has[size_t(k)] && start < 0) start = k;
                if (!has[size_t(k)] && start >= 0) {
                    flush(start, (k - 1 + n) % n);
                    start = -1;
                }
            }
            if (start >= 0) flush(start, (origin - 1 + n) % n);
        }
    }

    // ---- kerbs: alternating painted blocks, one block per sample
    for (int i = 0; i < n; ++i) {
        const int j = (i + 1) % n;
        const sim::CenterSample& c = t.center[size_t(i)];
        const sim::CenterSample& d = t.center[size_t(j)];
        if (!c.has_floor || !d.has_floor || t.on_stairs(c.s) || t.on_stairs(d.s)) continue;
        for (int side = 0; side < 2; ++side) {
            const int edge = side ? 5 : 1, base = side ? 6 : 0;
            const vec3 e0 = V(i, edge), e1 = V(j, edge), b0 = V(i, base), b1 = V(j, base);
            const float verge = glm::length(b0 - e0);
            if (verge < 0.5f) continue;
            const float w = std::min(0.7f, verge - 0.2f);
            const vec3 out0 = glm::normalize(b0 - e0), out1 = glm::normalize(b1 - e1);
            const float u0 = (i & 1) ? 0.5f : 0.0f, u1 = u0 + 0.5f;
            const vec3 l0 = c.up * 0.07f, l1 = d.up * 0.07f;
            // Top (v: 0 at the road edge .. 1 outside) and the face towards the road.
            vec3 p[4] = {e0 + l0, e1 + l1, e1 + out1 * w + l1 * 0.3f, e0 + out0 * w + l0 * 0.3f};
            vec2 q[4] = {r_kerb.at(u0, 0.0f), r_kerb.at(u1, 0.0f), r_kerb.at(u1, 1.0f), r_kerb.at(u0, 1.0f)};
            if (glm::dot(glm::cross(p[1] - p[0], p[2] - p[0]), c.up) < 0.0f) b.quad_uv(p[0], p[3], p[2], p[1], q[0], q[3], q[2], q[1], kWhite);
            else b.quad_uv(p[0], p[1], p[2], p[3], q[0], q[1], q[2], q[3], kWhite);
            vec3 f[4] = {e0, e1, e1 + l1, e0 + l0};
            vec2 fq[4] = {r_kerb.at(u0, 0.95f), r_kerb.at(u1, 0.95f), r_kerb.at(u1, 0.8f), r_kerb.at(u0, 0.8f)};
            if (glm::dot(glm::cross(f[1] - f[0], f[2] - f[0]), -out0) < 0.0f) b.quad_uv(f[0], f[3], f[2], f[1], fq[0], fq[3], fq[2], fq[1], kWhite);
            else b.quad_uv(f[0], f[1], f[2], f[3], fq[0], fq[1], fq[2], fq[3], kWhite);
        }
    }

    // ---- pennant flags on poles, alternating sides and designs
    for (int i = 3; i < n; i += 14) {
        const sim::CenterSample& c = t.center[size_t(i)];
        if (!c.has_floor) continue;
        const int k = i / 14;
        const int side = k % 2 ? 1 : -1;
        const bool walled = side > 0 ? right[size_t(i)] : left[size_t(i)];
        const vec3 foot = V(i, side > 0 ? 6 : 0) + c.right * float(side) * (walled ? 0.9f : 0.3f);
        const float h = 5.0f;
        region(b, r_pole);
        b.tube(foot - c.up * 0.5f, foot + c.up * h, 0.08f, 0.06f, kWhite, 6, true);
        region(b, r_finial);
        b.sphere(foot + c.up * (h + 0.08f), 0.13f, kWhite, 8, 5);
        b.uv_region_off();
        // A waving pennant: 4 columns x 2 rows, both faces.
        const UvRect& r = r_flag[k % 3];
        const vec3 top = foot + c.up * (h - 0.15f);
        const float len = 1.8f, height = 1.1f;
        auto pt = [&](float u, float v) {
            const float wave = 0.18f * std::sin(u * 5.0f + float(k)) * u;
            const float taper = 1.0f - 0.85f * u; // a pennant narrows to its tip
            return top + c.tangent * (u * len) + c.right * float(side) * wave - c.up * (height * (0.5f - (0.5f - v) * taper));
        };
        for (int x = 0; x < 4; ++x)
            for (int y = 0; y < 2; ++y) {
                const float u0 = float(x) / 4.0f, u1 = float(x + 1) / 4.0f, v0 = float(y) / 2.0f, v1 = float(y + 1) / 2.0f;
                const vec3 p00 = pt(u0, v0), p10 = pt(u1, v0), p11 = pt(u1, v1), p01 = pt(u0, v1);
                const vec2 q00 = r.at(u0, v0), q10 = r.at(u1, v0), q11 = r.at(u1, v1), q01 = r.at(u0, v1);
                b.quad_uv(p01, p11, p10, p00, q01, q11, q10, q00, kWhite);
                b.quad_uv(p00, p10, p11, p01, q00, q10, q11, q01, kWhite);
            }
    }
    m.mesh = b.take();
    m.finish();
    return m;
}

} // namespace gk::render
