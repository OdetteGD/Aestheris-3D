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
    engine/assets/ObjMeshLoader.cpp
    engine/world/DemoWorldInitializer.cpp
    engine/project/VirtualFileSystem.cpp
    engine/renderer/VulkanRenderer.cpp
    engine/renderer/OfflineEnvironment.cpp
    engine/renderer/GLES3Renderer.cpp
    engine/scripting/CSharpScriptEngine.cpp
    engine/renderer/AetherisValidationTracker.cpp
    engine/world/SceneNode.cpp
    engine/editor/EditorRaycast.cpp
    engine/editor/EditorGizmo.cpp
    engine/renderer/AetherisRenderGraph.cpp
    engine/renderer/GPUResourceManager.cpp
    engine/renderer/VulkanMobileProfile.cpp
    engine/renderer/TextureStreaming.cpp
    engine/renderer/TransientAliasPlanner.cpp
    android/AndroidHardwareBufferBridge.cpp
    android/JniBridge.cpp
    engine/shader/ShaderResourceManager.cpp
)



target_compile_features(aetheris3d PRIVATE cxx_std_20)

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
    -Wno-error=deprecated-declarations
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
