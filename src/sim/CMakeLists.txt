# gk_sim: the pure simulation. No rendering, windowing or OS calls, fixed 60 Hz
# step. The client (for prediction), the server and the bots all link it.
add_library(gk_sim STATIC
  stats.cpp
  track.cpp
  greybox_track.cpp
  kart.cpp
  world.cpp
  data.cpp
)
target_include_directories(gk_sim PUBLIC include)
target_link_libraries(gk_sim PUBLIC gk_core nlohmann_json::nlohmann_json PRIVATE Jolt gk_warnings)
target_compile_definitions(gk_sim PUBLIC GK_DATA_DIR="${PROJECT_SOURCE_DIR}/data")
