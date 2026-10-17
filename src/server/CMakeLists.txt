add_executable(gk_server main.cpp)
target_link_libraries(gk_server PRIVATE gk_net gk_warnings)

add_executable(gk_botclient bot_client.cpp)
target_link_libraries(gk_botclient PRIVATE gk_net gk_warnings)
