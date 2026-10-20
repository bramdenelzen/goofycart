# Sound synthesis and mixing; no audio device here (the client opens one).
add_library(gk_audio STATIC
  mixer.cpp
  music.cpp
  sfx.cpp
  voice.cpp
)
target_include_directories(gk_audio PUBLIC include)
target_link_libraries(gk_audio PRIVATE gk_warnings)

add_executable(gk_audiodemo demo.cpp)
target_link_libraries(gk_audiodemo PRIVATE gk_audio gk_warnings)
