# All third-party code is fetched at configure time at pinned versions, so a
# fresh clone builds with only CMake and a C++20 compiler. No Vulkan SDK is
# needed to build: headers come from Vulkan-Headers and the loader is opened
# at runtime by volk.
include(FetchContent)
# Some pinned dependencies still declare cmake_minimum_required below 3.5,
# which CMake 4 rejects without this.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
# Jolt keeps its CMakeLists in Build/, so it is populated and added by hand.
if(POLICY CMP0169)
  cmake_policy(SET CMP0169 OLD)
endif()
set(FETCHCONTENT_QUIET ON)

# Downloaded sources are kept in .deps/ (next to this repository's sources)
# and shared by every build folder, so only the very first configure on a
# machine downloads anything. Delete .deps/ to download afresh.
set(GK_DEPS_DIR "${PROJECT_SOURCE_DIR}/.deps" CACHE PATH "Shared download folder for third-party sources")
set(GK_FETCHED "")
macro(gk_fetch name repo tag)
  set(_dir "${GK_DEPS_DIR}/${name}-${tag}")
  string(TOUPPER "${name}" _up)
  if(EXISTS "${_dir}/.gk-complete")
    set(FETCHCONTENT_SOURCE_DIR_${_up} "${_dir}")
    FetchContent_Declare(${name} GIT_REPOSITORY ${repo} GIT_TAG ${tag} SYSTEM)
  else()
    set(_shallow TRUE)
    if("${tag}" MATCHES "^[0-9a-f]+$")
      set(_shallow FALSE) # a commit, which a shallow clone cannot name
    endif()
    FetchContent_Declare(${name} GIT_REPOSITORY ${repo} GIT_TAG ${tag} GIT_SHALLOW ${_shallow} SOURCE_DIR "${_dir}" SYSTEM)
    list(APPEND GK_FETCHED "${_dir}")
  endif()
endmacro()

# --- always (sim, server, tests) ---------------------------------------------
gk_fetch(glm https://github.com/g-truc/glm.git 1.0.1)
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLM_ENABLE_CXX_20 ON CACHE BOOL "" FORCE)

gk_fetch(nlohmann_json https://github.com/nlohmann/json.git v3.11.3)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)

gk_fetch(JoltPhysics https://github.com/jrouwe/JoltPhysics.git v5.3.0)
set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
set(TARGET_VIEWER OFF CACHE BOOL "" FORCE)
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(CROSS_PLATFORM_DETERMINISTIC OFF CACHE BOOL "" FORCE)
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
set(OVERRIDE_CXX_FLAGS OFF CACHE BOOL "" FORCE)
set(CPP_RTTI_ENABLED ON CACHE BOOL "" FORCE)
set(CPP_EXCEPTIONS_ENABLED ON CACHE BOOL "" FORCE)

gk_fetch(enet https://github.com/lsalzman/enet.git v1.3.18)

FetchContent_MakeAvailable(glm nlohmann_json enet)
if(WIN32)
  target_link_libraries(enet PUBLIC ws2_32 winmm)
  # enet.h pulls in <windows.h>; keep its min/max macros out of our code.
  target_compile_definitions(enet INTERFACE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
target_include_directories(enet SYSTEM INTERFACE ${enet_SOURCE_DIR}/include)
FetchContent_GetProperties(JoltPhysics)
if(NOT joltphysics_POPULATED)
  FetchContent_Populate(JoltPhysics)
  add_subdirectory(${joltphysics_SOURCE_DIR}/Build ${joltphysics_BINARY_DIR} EXCLUDE_FROM_ALL SYSTEM)
  # One translation unit per 16 sources: Jolt is otherwise the slowest part of a clean build.
  set_target_properties(Jolt PROPERTIES UNITY_BUILD ON UNITY_BUILD_BATCH_SIZE 16)
endif()

# HTTP(S) for accounts and matchmaking (master server, game servers, client).
# Header-only; HTTPS when OpenSSL 3 is found, linked statically where the
# platform has no system copy to rely on (Windows, macOS).
gk_fetch(httplib https://github.com/yhirose/cpp-httplib.git v0.20.1)
FetchContent_GetProperties(httplib)
if(NOT httplib_POPULATED)
  FetchContent_Populate(httplib)
endif()
add_library(gk_httplib INTERFACE)
target_include_directories(gk_httplib SYSTEM INTERFACE ${httplib_SOURCE_DIR})
find_package(Threads REQUIRED)
target_link_libraries(gk_httplib INTERFACE Threads::Threads)
if(WIN32)
  target_link_libraries(gk_httplib INTERFACE ws2_32 crypt32)
endif()
option(GK_HTTPS "Build HTTPS support (needs OpenSSL 3)" ON)
if(GK_HTTPS)
  if(APPLE AND NOT OPENSSL_ROOT_DIR)
    execute_process(COMMAND brew --prefix openssl@3 OUTPUT_VARIABLE _brew_ssl OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(_brew_ssl)
      set(OPENSSL_ROOT_DIR ${_brew_ssl})
    endif()
  endif()
  if(WIN32 OR APPLE)
    set(OPENSSL_USE_STATIC_LIBS ON)
  endif()
  if(MSVC)
    set(OPENSSL_MSVC_STATIC_RT OFF)
  endif()
  find_package(OpenSSL 3.0)
endif()
if(OPENSSL_FOUND)
  target_compile_definitions(gk_httplib INTERFACE CPPHTTPLIB_OPENSSL_SUPPORT GK_HAVE_HTTPS=1)
  target_link_libraries(gk_httplib INTERFACE OpenSSL::SSL OpenSSL::Crypto)
  if(APPLE) # trust the system keychain's roots
    target_compile_definitions(gk_httplib INTERFACE CPPHTTPLIB_USE_CERTS_FROM_MACOSX_KEYCHAIN)
    target_link_libraries(gk_httplib INTERFACE "-framework CoreFoundation" "-framework Security")
  endif()
  message(STATUS "HTTPS: OpenSSL ${OPENSSL_VERSION}")
else()
  message(STATUS "HTTPS: OpenSSL not found, the master server client speaks plain HTTP only")
endif()

if(GK_BUILD_TESTS)
  gk_fetch(doctest https://github.com/doctest/doctest.git v2.4.11)
  set(DOCTEST_WITH_TESTS OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(doctest)
endif()

# --- client only --------------------------------------------------------------
if(GK_BUILD_CLIENT)
  gk_fetch(SDL3 https://github.com/libsdl-org/SDL.git release-3.2.20)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  # Only what the game uses: a Vulkan window, input, audio. SDL's 2D renderer,
  # GPU API, OpenGL and camera are a large part of its build time.
  foreach(_off SDL_RENDER SDL_GPU SDL_CAMERA SDL_OPENGL SDL_OPENGLES)
    set(${_off} OFF CACHE BOOL "" FORCE)
  endforeach()

  gk_fetch(VulkanHeaders https://github.com/KhronosGroup/Vulkan-Headers.git vulkan-sdk-1.4.304.0)
  gk_fetch(volk https://github.com/zeux/volk.git vulkan-sdk-1.4.304.0)
  set(VOLK_PULL_IN_VULKAN OFF CACHE BOOL "" FORCE)
  gk_fetch(vkbootstrap https://github.com/charles-lunarg/vk-bootstrap.git v1.3.302)
  set(VK_BOOTSTRAP_TEST OFF CACHE BOOL "" FORCE)
  gk_fetch(VulkanMemoryAllocator https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git v3.2.1)
  gk_fetch(stb https://github.com/nothings/stb.git 2c980bb59875b0d32144a71867fbdebb2f77cd20)

  FetchContent_MakeAvailable(SDL3 VulkanHeaders)
  FetchContent_MakeAvailable(volk vkbootstrap VulkanMemoryAllocator)
  target_link_libraries(volk PUBLIC Vulkan::Headers)

  FetchContent_GetProperties(stb)
  if(NOT stb_POPULATED)
    FetchContent_Populate(stb)
  endif()
  add_library(gk_stb INTERFACE)
  target_include_directories(gk_stb SYSTEM INTERFACE ${stb_SOURCE_DIR})
endif()

# Mark what this configure downloaded as complete, for the next build folder.
foreach(_dir IN LISTS GK_FETCHED)
  if(EXISTS "${_dir}")
    file(TOUCH "${_dir}/.gk-complete")
  endif()
endforeach()
