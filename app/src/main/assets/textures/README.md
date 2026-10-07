# Offline Environment and IBL Assets

Aetheris loads these immutable boot-time assets from `assets/textures/`:

- `environment.ktx2` — optional full environment cubemap with mip chain.
- `environment.hdr` — optional Radiance RGBE equirectangular fallback.
- `ibl_irradiance.ktx2` — offline diffuse irradiance cubemap.
- `ibl_prefiltered.ktx2` — offline GGX-prefiltered specular cubemap with mip levels.

The native loader accepts raw, uncompressed KTX2 cubemaps with six faces and up to 16 mip levels using R16G16B16A16_SFLOAT or R32G32B32A32_SFLOAT.

These files are copied from APK assets into the app's private project sandbox once at startup. The renderer creates Vulkan images, immutable samplers, and descriptor sets during initialization; frame production only samples the existing resources.

Place production HDR/KTX2 assets here before packaging. The repository's minimal safety cubemap is only a failure-path guard and is not the intended lighting source.