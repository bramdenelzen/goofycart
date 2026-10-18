#pragma once
#include <cstdint>
#include <vector>

namespace gk::render {

// Surface textures, made procedurally (no image files): one 128 x 128 layer
// each in a texture array. They are mostly grey detail that
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

constexpr int kTexSize = 128;
constexpr int kTexMips = 8; // 128 .. 1

// Every layer's mip chain, RGBA8: layer 0 mips 0..5, then layer 1, ...
std::vector<uint8_t> make_texture_layers();

} // namespace gk::render
