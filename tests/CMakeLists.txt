add_executable(gk_tests
  test_main.cpp
  test_core.cpp
  test_kart.cpp
  test_race.cpp
  test_net.cpp
  test_items.cpp
  test_ai.cpp
  test_tracks.cpp
  test_audio.cpp
  test_ghost.cpp
  test_props.cpp
  test_paths.cpp
  test_master.cpp
)
target_link_libraries(gk_tests PRIVATE gk_net gk_audio gk_online doctest::doctest gk_warnings)
add_test(NAME gk_tests COMMAND gk_tests)
