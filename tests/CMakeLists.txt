add_executable(gk_tests
  test_main.cpp
  test_core.cpp
  test_kart.cpp
  test_race.cpp
)
target_link_libraries(gk_tests PRIVATE gk_sim doctest::doctest gk_warnings)
add_test(NAME gk_tests COMMAND gk_tests)
