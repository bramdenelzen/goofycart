#pragma once
// A signed-distance-field font baked from a TrueType file at start-up, so
// UI text stays smooth at any size and can carry outlines and glows.
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace gk::render {

class SdfFont {
public:
    struct Glyph {
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0; // quad around the pen, in em units (y down, baseline 0)
        float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
        float advance = 0;                    // em units
        bool valid = false;
    };
    static constexpr int kFirst = 32, kLast = 126;
    // The distance field reaches this many em units either side of an edge.
    float spread() const { return spread_em_; }

    bool load(const std::string& ttf_path);
    bool loaded() const { return !atlas_.empty(); }
    const Glyph& glyph(char c) const;
    float kern(char a, char b) const;
    float ascent() const { return ascent_; }   // em units above the baseline
    float cap_height() const { return cap_; }
    int atlas_size() const { return size_; }
    const std::vector<uint8_t>& atlas_rgba() const { return atlas_; }

private:
    std::vector<uint8_t> ttf_;
    std::vector<uint8_t> atlas_;
    int size_ = 0;
    std::array<Glyph, kLast - kFirst + 1> glyphs_{};
    std::vector<float> kern_; // (a - kFirst) * n + (b - kFirst)
    float ascent_ = 0.8f, cap_ = 0.7f, spread_em_ = 0.1f;
};

} // namespace gk::render
