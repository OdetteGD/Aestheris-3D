# Aetheris Engine

Aetheris is a mobile-first C++20/Android NDK 3D runtime/editor foundation.

## Mobile renderer backbone

The renderer uses a Vulkan 1.1 baseline. Vulkan 1.2 promotions and extensions are capability-gated instead of hard requirements, preserving a fallback path for older Mali/Adreno implementations.

### RenderGraph

AetherisRenderGraph is a fixed-capacity declarative DAG. Passes call ReadResource() and WriteResource(). Compile performs dependency construction, dead-pass culling, Kahn topological sorting and grouped image barriers. Frame execution uses fixed arrays and does not allocate.

Graphics, compute and transfer pass types share the same graph boundary. The graph is intended to host GPU frustum/occlusion culling, bloom upsample, CSM atlas passes and final editor composition.

### GPUResourceManager

GPUResourceManager owns Vulkan device-memory blocks, image/buffer bindings, descriptor pools, pipeline-layout caching and persistent VkPipelineCache. The cache can be persisted under:

AetherisProject/cache/pipelines/aetheris_vk.bin

Resource creation is initialization/streaming work; the frame path is fixed-capacity. TransientAliasPlanner assigns non-overlapping lifetimes to compatible memory slots, and CreateAliasedImage() binds compatible Vulkan images to the same allocation.

### TBDR/mobile policy

MobileAttachmentPolicy and MobileSubpassPlan provide the Vulkan 1.1 render-pass/subpass boundary for tile-friendly attachments. Transient attachments use VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT only with attachment-compatible usage, while G-buffer/depth data can remain in subpass/input-attachment workflows.

Vendor-specific QCOM/ARM tile extensions are optional optimization paths, never hard requirements.

### Android editor viewport

AetherisEditorViewport is a SurfaceView boundary with surfaceCreated, surfaceChanged and surfaceDestroyed callbacks. JNI converts the Java Surface with ANativeWindow_fromSurface() and explicitly releases the native window when the surface dies.

AndroidHardwareBufferBridge is capability-gated for VK_ANDROID_external_memory_android_hardware_buffer. It does not treat an opaque integer as a portable GPU image. The portable default is the SurfaceView; zero-copy AHardwareBuffer import is only enabled after format/usage compatibility is queried.

### Shader fallback

Vulkan consumes precompiled SPIR-V. GLES 3.x uses native GLSL or an offline SPIR-V-Cross translation/import path. Translation never occurs on the render thread.

### Runtime allocation rule

No STL container growth, shader compilation, graph construction or descriptor-pool creation is permitted inside frame execution. Initialization, asset streaming and graph compilation are explicit non-frame phases.

## Project VFS

The project root contains:
assets/models
assets/textures
assets/shaders/glsl
assets/shaders/spirv
assets/materials
assets/scenes
cache/pipelines
cache/shader
cache/derived

The Android layer should pass Context.getExternalFilesDir(null)?.absolutePath (or an app-private filesDir path) into AetherisNative.nativeSetProjectRoot() rather than hard-coding a storage path.

## Current milestone

This PR replaces the shader-first direction with the engine backbone: render graph, GPU resource/memory layer, transient alias planning, persistent pipeline cache, mobile attachment/subpass policy, texture-streaming boundary, Android editor surface lifecycle, and GLES fallback architecture.

Production PBR/CSM/bloom shader execution and full AHardwareBuffer zero-copy UI composition remain consumers of this backbone rather than being hidden inside the resource layer.
