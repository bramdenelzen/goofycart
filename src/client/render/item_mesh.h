#pragma once
#include "mesh.h"

namespace gk::render {

// Low-poly placeholder models for items, boxes and item effects.
MeshData make_item_box_mesh();
MeshData make_banana_mesh();
MeshData make_shell_mesh(uint32_t color, bool spikes);
MeshData make_bomb_mesh();
MeshData make_ball_mesh(uint32_t color);   // fireballs and explosions (tinted)
MeshData make_boomerang_mesh();
MeshData make_bullet_mesh();               // fits around a kart
MeshData make_chomper_mesh();

} // namespace gk::render
