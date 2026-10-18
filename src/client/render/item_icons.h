#pragma once
// Item icons for the HUD, painted once into a 512 x 512 RGBA atlas: a
// 5 x 5 grid, one cell per sim::Item (index = item value - 1).
#include <cstdint>
#include <vector>

namespace gk::render {

inline constexpr int kIconGrid = 5;
std::vector<uint8_t> paint_item_icons(int size = 512);

} // namespace gk::render
