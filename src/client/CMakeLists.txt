set(GK_SHADERS
  ${PROJECT_SOURCE_DIR}/shaders/mesh.vert ${PROJECT_SOURCE_DIR}/shaders/mesh.frag
  ${PROJECT_SOURCE_DIR}/shaders/ui.vert ${PROJECT_SOURCE_DIR}/shaders/ui.frag)

# Compile shaders at build time when glslangValidator is available, otherwise
# fall back to the checked-in SPIR-V headers in shaders/prebuilt.
find_program(GLSLANG_VALIDATOR glslangValidator HINTS $ENV{VULKAN_SDK}/bin)
find_package(Python3 COMPONENTS Interpreter)
if(GLSLANG_VALIDATOR AND Python3_FOUND)
  set(GK_SHADER_OUT ${CMAKE_CURRENT_BINARY_DIR}/shaders)
  set(GK_SHADER_HEADERS)
  foreach(src ${GK_SHADERS})
    get_filename_component(name ${src} NAME)
    string(REPLACE "." "_" stem ${name})
    list(APPEND GK_SHADER_HEADERS ${GK_SHADER_OUT}/${stem}.h)
  endforeach()
  add_custom_command(
    OUTPUT ${GK_SHADER_HEADERS}
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/compile_shaders.py ${GLSLANG_VALIDATOR} ${GK_SHADER_OUT} ${GK_SHADERS}
    DEPENDS ${GK_SHADERS} ${PROJECT_SOURCE_DIR}/tools/compile_shaders.py
    COMMENT "Compiling shaders to SPIR-V")
  add_custom_target(gk_shaders DEPENDS ${GK_SHADER_HEADERS})
  set(GK_SHADER_INCLUDE ${GK_SHADER_OUT})
else()
  message(STATUS "glslangValidator not found: using prebuilt SPIR-V from shaders/prebuilt")
  add_custom_target(gk_shaders)
  set(GK_SHADER_INCLUDE ${PROJECT_SOURCE_DIR}/shaders/prebuilt)
endif()

add_executable(gk_client
  main.cpp
  app.cpp
  input.cpp
  hud.cpp
  menu.cpp
  frontend.cpp
  audio_device.cpp
  settings.cpp
  render/mesh.cpp
  render/textures.cpp
  render/track_mesh.cpp
  render/kart_mesh.cpp
  render/item_mesh.cpp
  render/renderer.cpp
)
add_dependencies(gk_client gk_shaders)
target_include_directories(gk_client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${GK_SHADER_INCLUDE})
target_link_libraries(gk_client PRIVATE gk_net gk_audio gk_online SDL3::SDL3 volk vk-bootstrap::vk-bootstrap
                      GPUOpen::VulkanMemoryAllocator gk_stb gk_warnings)
if(WIN32)
  set_target_properties(gk_client PROPERTIES WIN32_EXECUTABLE $<CONFIG:Release>)
endif()

if(APPLE AND GK_MACOS_BUNDLE)
  # "Gobby Kart.app": data in Resources, MoltenVK (from Homebrew's molten-vk,
  # or MOLTENVK_DYLIB) in Frameworks so it runs without the Vulkan SDK.
  set_target_properties(gk_client PROPERTIES
    MACOSX_BUNDLE ON
    MACOSX_BUNDLE_BUNDLE_NAME "Gobby Kart"
    MACOSX_BUNDLE_GUI_IDENTIFIER "net.gobbykart.client"
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
  if(NOT MOLTENVK_DYLIB)
    execute_process(COMMAND brew --prefix molten-vk OUTPUT_VARIABLE _mvk OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(_mvk AND EXISTS "${_mvk}/lib/libMoltenVK.dylib")
      set(MOLTENVK_DYLIB "${_mvk}/lib/libMoltenVK.dylib")
    endif()
  endif()
  if(MOLTENVK_DYLIB)
    install(FILES ${MOLTENVK_DYLIB} DESTINATION "gk_client.app/Contents/Frameworks" COMPONENT game)
  else()
    message(WARNING "MoltenVK not found (brew install molten-vk): the app will need the Vulkan SDK installed")
  endif()
endif()
