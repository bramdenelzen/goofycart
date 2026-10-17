# Developer tool: top-down PNGs of the tracks (see trackmap.cpp).
add_executable(gk_trackmap trackmap.cpp stb_impl.cpp)
target_link_libraries(gk_trackmap PRIVATE gk_sim gk_stb gk_warnings)
