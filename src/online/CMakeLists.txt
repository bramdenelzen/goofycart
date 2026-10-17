# Accounts, matchmaking and ratings: the master server's logic and HTTP API,
# and the client that game servers and players use to reach it.
add_library(gk_online STATIC
  master_state.cpp
  sha256.cpp
  master_http.cpp
  master_client.cpp
)
target_include_directories(gk_online PUBLIC include)
target_link_libraries(gk_online PUBLIC gk_httplib gk_core PRIVATE nlohmann_json::nlohmann_json gk_warnings)

add_executable(gk_master master_main.cpp)
target_link_libraries(gk_master PRIVATE gk_online gk_core gk_httplib gk_warnings)
