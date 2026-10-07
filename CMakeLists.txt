cmake_minimum_required(VERSION 3.22.1)
project(AetherisEngine LANGUAGES CXX)

# One native boundary: Kotlin/JNI, renderer, graph, resource manager and Android helpers
# are linked into the single libaetheris3d.so production artifact.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(aetheris3d SHARED
    engine/core/EngineCore.cpp
    engine/project/VirtualFileSystem.cpp
    engine/renderer/VulkanRenderer.cpp
    engine/renderer/OpenGLESRenderer.cpp
    engine/renderer/AetherisRenderGraph.cpp
    engine/renderer/GPUResourceManager.cpp
    engine/renderer/VulkanMobileProfile.cpp
    engine/renderer/TextureStreaming.cpp
    engine/renderer/TransientAliasPlanner.cpp
    android/AndroidHardwareBufferBridge.cpp
    android/JniBridge.cpp
    engine/shader/ShaderResourceManager.cpp
)


# Vulkan shader toolchain. Android NDK shader-tools provides glslc; shaders are
# compiled at build time so the APK contains SPIR-V and the render thread never
# performs shader compilation or file parsing.
find_program(AETHERIS_GLSLC glslc
    HINTS
        "$ENV{ANDROID_NDK_HOME}/shader-tools"
        "$ENV{ANDROID_NDK}/shader-tools"
)

if(NOT AETHERIS_GLSLC)
    message(FATAL_ERROR "Aetheris requires glslc from the Android NDK shader-tools directory")
endif()

set(AETHERIS_SHADER_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/app/src/main/assets/shaders/spirv")
set(AETHERIS_SHADER_SOURCES
    "${CMAKE_SOURCE_DIR}/shaders/gbuffer_mobile.vert"
    "${CMAKE_SOURCE_DIR}/shaders/gbuffer_mobile.frag"
    "${CMAKE_SOURCE_DIR}/shaders/fullscreen_triangle.vert"
    "${CMAKE_SOURCE_DIR}/shaders/deferred_lighting_mobile.frag"
    "${CMAKE_SOURCE_DIR}/shaders/bloom_aces_mobile.frag"
)

set(AETHERIS_SHADER_OUTPUTS)
foreach(SHADER_SOURCE IN LISTS AETHERIS_SHADER_SOURCES)
    get_filename_component(SHADER_NAME "${SHADER_SOURCE}" NAME)
    set(SHADER_OUTPUT "${AETHERIS_SHADER_OUTPUT_DIR}/${SHADER_NAME}.spv")
    add_custom_command(
        OUTPUT "${SHADER_OUTPUT}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${AETHERIS_SHADER_OUTPUT_DIR}"
        COMMAND "${AETHERIS_GLSLC}" --target-env=vulkan1.1 -O "${SHADER_SOURCE}" -o "${SHADER_OUTPUT}"
        DEPENDS "${SHADER_SOURCE}"
        COMMENT "Compiling Aetheris Vulkan shader ${SHADER_NAME}"
        VERBATIM
    )
    list(APPEND AETHERIS_SHADER_OUTPUTS "${SHADER_OUTPUT}")
endforeach()

add_custom_target(aetheris_shaders ALL DEPENDS ${AETHERIS_SHADER_OUTPUTS})
add_dependencies(aetheris3d aetheris_shaders)

target_include_directories(aetheris3d PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

find_library(log-lib log)
find_library(android-lib android)

target_link_libraries(aetheris3d PRIVATE
    ${log-lib}
    ${android-lib}
    vulkan
    EGL
    GLESv3
)

target_compile_definitions(aetheris3d PRIVATE
    AETHERIS_ANDROID=1
    VK_USE_PLATFORM_ANDROID_KHR=1
)

target_compile_options(aetheris3d PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wshadow
    -Werror=return-type
    -O3
    -flto
    -fvisibility=hidden
    -ffunction-sections
    -fdata-sections
)

target_link_options(aetheris3d PRIVATE
    -flto
    -Wl,--gc-sections
)

set_target_properties(aetheris3d PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)

# Use ccache when the Android build environment provides it.
if(DEFINED CMAKE_C_COMPILER_LAUNCHER)
    set_property(TARGET aetheris3d PROPERTY C_COMPILER_LAUNCHER "${CMAKE_C_COMPILER_LAUNCHER}")
endif()
if(DEFINED CMAKE_CXX_COMPILER_LAUNCHER)
    set_property(TARGET aetheris3d PROPERTY CXX_COMPILER_LAUNCHER "${CMAKE_CXX_COMPILER_LAUNCHER}")
endif()
