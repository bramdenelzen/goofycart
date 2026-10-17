#pragma once
#include <cstdint>
#include <vector>

namespace gk::render {

// Low-resolution surface textures, made procedurally (no image files): one
// 32 x 32 layer each in a texture array. They are mostly grey detail that
// multiplies the vertex colour, so one texture serves every theme's palette.
enum Tex : uint32_t {
    TexWhite,   // no detail: the vertex colour as is
    TexAsphalt,
    TexGrass,
    TexDirt,
    TexSand,
    TexSnow,
    TexIce,
    TexWater,
    TexBrick,
    TexPanel,
    TexWood,
    TexRock,
    TexArrows,  // chevrons pointing along +v
    TexStripes,
    TexLeaves,
    TexBark,
    TexWindows,
    TexTread,
    TexCloud,
    TexCount
};

constexpr int kTexSize = 32;
constexpr int kTexMips = 6; // 32 .. 1

// Every layer's mip chain, RGBA8: layer 0 mips 0..5, then layer 1, ...
std::vector<uint8_t> make_texture_layers();

} // namespace gk::render
