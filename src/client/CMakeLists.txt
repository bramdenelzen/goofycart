set(GK_SHADERS ${PROJECT_SOURCE_DIR}/shaders/mesh.vert ${PROJECT_SOURCE_DIR}/shaders/mesh.frag)

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
  render/mesh.cpp
  render/renderer.cpp
)
add_dependencies(gk_client gk_shaders)
target_include_directories(gk_client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${GK_SHADER_INCLUDE})
target_link_libraries(gk_client PRIVATE gk_sim SDL3::SDL3 volk vk-bootstrap::vk-bootstrap
                      GPUOpen::VulkanMemoryAllocator gk_stb gk_warnings)
if(WIN32)
  set_target_properties(gk_client PROPERTIES WIN32_EXECUTABLE $<CONFIG:Release>)
endif()
