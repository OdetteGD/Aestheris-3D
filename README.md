# Aetheris Engine
Mobile-first C++20/Android NDK renderer foundation. Vulkan 1.2 is primary; OpenGL ES 3.x is an isolated fallback boundary.

## Project VFS
Example project root:
`/storage/emulated/0/Android/data/com.aetheris.engine/files/AetherisProjects/NewProject/`
Mount:
- assets/models
- assets/textures
- assets/shaders/glsl
- assets/shaders/spirv
- assets/materials
- assets/scenes
- cache/pipelines
- cache/shader
- cache/derived

## Runtime architecture
EngineCore -> IAetherisRenderer -> VulkanRenderer/OpenGLESRenderer.
API switching is serialized by EngineCore. A switch is rejected during an active frame; otherwise the old backend is Shutdown/reset before constructing the new backend, while CPU scene state remains alive.

## Vulkan synchronization
Three Frame records each own a command pool/buffer, image-available semaphore, render-finished semaphore and signaled fence. Acquire waits on the frame fence, submit signals render-finished, present waits on it, then the frame index rotates modulo 3. OUT_OF_DATE and SUBOPTIMAL trigger device-idle swapchain rebuild.

## Visual pipeline
The intended production RenderGraph is:
Depth/HiZ -> CSM -> opaque PBR/GGX + IBL -> transparent -> bloom downsample pyramid -> bloom upsample -> ACES tone map -> UI.
PBR uses metallic/roughness/AO, irradiance, prefiltered environment and BRDF LUT. CSM uses a four-cascade array and 3x3 PCF.

## Shader compilation
Ship precompiled SPIR-V in assets/shaders/spirv for deterministic startup. Use shaderc only in an editor/import worker, cache its output by source+defines+device/driver key, and never compile on the render thread. GLES consumes GLSL ES 3.00/3.10 source.

## Important status
This branch is the renderer foundation. The Vulkan swapchain/frame scheduler is implemented; RenderGraph resource allocation, descriptor/pipeline creation, real offscreen editor targets, KTX2/ASTC streaming and persistent VkPipelineCache are deliberately the next integration layer rather than pretending the clear-pass foundation is already a complete AAA renderer.
