# `cmake --install` and `cpack` lay the game out as one portable folder:
#
#   gk_client  gk_server  gk_master  gk_botclient  data/  docs/
#
# and zip it (Windows), tar it (Linux) or, with GK_MACOS_BUNDLE, make
# "Gobby Kart.app" (data in Resources, MoltenVK in Frameworks) in a DMG.
set(GK_PROGRAMS)
foreach(t gk_client gk_server gk_botclient gk_master)
  if(TARGET ${t})
    list(APPEND GK_PROGRAMS ${t})
  endif()
endforeach()
install(TARGETS ${GK_PROGRAMS} RUNTIME DESTINATION . COMPONENT game BUNDLE DESTINATION . COMPONENT game)
if(APPLE AND GK_MACOS_BUNDLE AND TARGET gk_client)
  install(DIRECTORY ${PROJECT_SOURCE_DIR}/data DESTINATION "gk_client.app/Contents/Resources" COMPONENT game)
endif()
install(DIRECTORY ${PROJECT_SOURCE_DIR}/data DESTINATION . COMPONENT game)
install(FILES ${PROJECT_SOURCE_DIR}/README.md DESTINATION . COMPONENT game)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/docs/ DESTINATION docs COMPONENT game)
# Double-click scripts to host or join a server (tools/host).
if(WIN32)
  install(PROGRAMS "${PROJECT_SOURCE_DIR}/tools/host/Host a Server.bat" "${PROJECT_SOURCE_DIR}/tools/host/Join a Server.bat"
    "${PROJECT_SOURCE_DIR}/tools/host/Dedicated Server.bat"
          DESTINATION . COMPONENT game)
elseif(APPLE)
  install(PROGRAMS "${PROJECT_SOURCE_DIR}/tools/host/Host a Server.command"
    "${PROJECT_SOURCE_DIR}/tools/host/Dedicated Server.command"
          "${PROJECT_SOURCE_DIR}/tools/host/Join a Server.command" DESTINATION . COMPONENT game)
endif()

if(MSVC)
  # The C++ runtime DLLs next to the programs, so no redistributable is needed.
  set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION .)
  set(CMAKE_INSTALL_SYSTEM_RUNTIME_COMPONENT game)
  include(InstallRequiredSystemLibraries)
endif()

# Only the game; dependencies' own install rules (headers, CMake files) stay out.
set(CPACK_COMPONENTS_ALL game)
set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
set(CPACK_DMG_COMPONENT_INSTALL ON)
set(CPACK_COMPONENTS_GROUPING ALL_COMPONENTS_IN_ONE)
set(CPACK_PACKAGE_NAME "GobbyKart")
set(CPACK_PACKAGE_VENDOR "Gobby Kart")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Low-poly multiplayer kart racer")
string(TOLOWER "${CMAKE_SYSTEM_NAME}" _os)
if(_os STREQUAL "darwin")
  set(_os macos)
endif()
set(_arch ${CMAKE_SYSTEM_PROCESSOR})
if(_arch MATCHES "AMD64|x86_64")
  set(_arch x64)
elseif(_arch MATCHES "arm64|aarch64")
  set(_arch arm64)
endif()
set(CPACK_PACKAGE_FILE_NAME "GobbyKart-${PROJECT_VERSION}-${_os}-${_arch}")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
set(CPACK_COMPONENT_INCLUDE_TOPLEVEL_DIRECTORY ON)
set(CPACK_STRIP_FILES ON)
if(WIN32)
  set(CPACK_GENERATOR ZIP)
elseif(APPLE AND GK_MACOS_BUNDLE)
  set(CPACK_GENERATOR DragNDrop)
  set(CPACK_DMG_VOLUME_NAME "Gobby Kart")
else()
  set(CPACK_GENERATOR TGZ)
endif()
include(CPack)
