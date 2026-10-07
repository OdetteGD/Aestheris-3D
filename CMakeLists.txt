cmake_minimum_required(VERSION 3.22.1)
project(AetherisEngine LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_library(aetheris SHARED
 engine/core/EngineCore.cpp engine/project/VirtualFileSystem.cpp
 engine/renderer/VulkanRenderer.cpp engine/renderer/OpenGLESRenderer.cpp
 engine/renderer/AetherisRenderGraph.cpp engine/renderer/GPUResourceManager.cpp
 engine/renderer/VulkanMobileProfile.cpp engine/renderer/TextureStreaming.cpp engine/renderer/TransientAliasPlanner.cpp android/AndroidHardwareBufferBridge.cpp
 android/AetherisJNI.cpp engine/shader/ShaderResourceManager.cpp)
target_include_directories(aetheris PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
find_library(log-lib log);find_library(android-lib android)
target_link_libraries(aetheris PRIVATE ${log-lib} ${android-lib} vulkan EGL GLESv3)
target_compile_definitions(aetheris PRIVATE AETHERIS_ANDROID=1)
target_compile_options(aetheris PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror=return-type)
set_target_properties(aetheris PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
