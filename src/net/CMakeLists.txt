# gk_net: wire protocol, the server's race room, client prediction and the
# ENet transport. Everything but transport.cpp is transport-agnostic so tests
# can run whole races over a simulated link.
add_library(gk_net STATIC
  protocol.cpp
  room.cpp
  client_game.cpp
  transport.cpp
)
target_include_directories(gk_net PUBLIC include)
target_link_libraries(gk_net PUBLIC gk_sim PRIVATE enet gk_warnings)
