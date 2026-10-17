#pragma once
#include "input.h"
#include "render/renderer.h"

#include "gk/net/protocol.h"
#include "gk/sim/stats.h"

namespace gk {

// Kart selection, used offline before a race and online as the lobby.
struct SelectState {
    sim::Build build{1, 0, 0, 0};
    int row = 0;       // 0 driver, 1 body, 2 tires, 3 glider
    bool ready = false;
};

enum class SelectAction { None, Changed, Accept, Back };

SelectAction update_select(SelectState& s, const ClientActions& act, const sim::PartsCatalog& parts);

// Draws the part pickers and stat bars on the left; online, the lobby
// (players, ready flags, Grand Prix points and the timer) on the right.
void draw_select(render::Renderer& r, const SelectState& s, const sim::PartsCatalog& parts,
                 const net::LobbyMsg* lobby);

} // namespace gk
