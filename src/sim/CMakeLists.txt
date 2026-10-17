# gk_sim: the pure simulation. No rendering, windowing or OS calls, fixed 60 Hz
# step. The client (for prediction), the server and the bots all link it.
add_library(gk_sim STATIC
  world.cpp
)
target_include_directories(gk_sim PUBLIC include)
target_link_libraries(gk_sim PUBLIC gk_core nlohmann_json::nlohmann_json Jolt PRIVATE gk_warnings)
