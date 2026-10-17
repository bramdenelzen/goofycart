add_library(gk_core STATIC
  log.cpp
)
target_include_directories(gk_core PUBLIC include)
target_link_libraries(gk_core PUBLIC glm::glm PRIVATE gk_warnings)
target_compile_definitions(gk_core PUBLIC GLM_FORCE_DEPTH_ZERO_TO_ONE GLM_FORCE_RADIANS GLM_ENABLE_EXPERIMENTAL)
