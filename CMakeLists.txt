cmake_minimum_required(VERSION 3.24)
project(GobbyKart VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(GK_BUILD_CLIENT "Build the game client (needs SDL3 + Vulkan headers)" ON)
option(GK_BUILD_SERVER "Build the headless dedicated server" ON)
option(GK_BUILD_TESTS "Build unit tests" ON)
option(GK_MACOS_BUNDLE "Build the macOS client as an app bundle with MoltenVK inside" OFF)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# ccache (or sccache) makes every rebuild after the first, even from a clean
# build folder, take seconds: brew install ccache / choco install ccache / apt install ccache.
option(GK_COMPILER_CACHE "Use ccache or sccache when installed" ON)
if(GK_COMPILER_CACHE AND NOT CMAKE_CXX_COMPILER_LAUNCHER)
  find_program(GK_CACHE_PROGRAM NAMES ccache sccache)
  if(GK_CACHE_PROGRAM MATCHES "ccache")
    # Jolt and SDL use precompiled headers, which ccache only caches when told
    # to ignore their timestamps.
    set(_launcher ${CMAKE_COMMAND} -E env
        "CCACHE_SLOPPINESS=pch_defines,time_macros,include_file_mtime,include_file_ctime" ${GK_CACHE_PROGRAM})
    set(CMAKE_C_COMPILER_LAUNCHER ${_launcher})
    set(CMAKE_CXX_COMPILER_LAUNCHER ${_launcher})
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
      add_compile_options("SHELL:-Xclang -fno-pch-timestamp")
    endif()
  elseif(GK_CACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER ${GK_CACHE_PROGRAM})
    set(CMAKE_CXX_COMPILER_LAUNCHER ${GK_CACHE_PROGRAM})
  endif()
  if(GK_CACHE_PROGRAM)
    message(STATUS "Compiler cache: ${GK_CACHE_PROGRAM}")
  endif()
endif()
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

include(Dependencies)

# Shared warning flags for our own targets (not third-party code).
add_library(gk_warnings INTERFACE)
if(MSVC)
  target_compile_options(gk_warnings INTERFACE /W4 /permissive- /utf-8)
else()
  target_compile_options(gk_warnings INTERFACE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
endif()

add_subdirectory(src/core)
add_subdirectory(src/sim)
add_subdirectory(src/net)
add_subdirectory(src/audio)
add_subdirectory(src/online)
if(GK_BUILD_SERVER)
  add_subdirectory(src/server)
endif()
if(GK_BUILD_CLIENT)
  add_subdirectory(src/client)
endif()
if(GK_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

include(Packaging)
