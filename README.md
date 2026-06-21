# Roadmap

## Implemented Now

- [x] Vulkan swapchain creation and presentation
- [x] GLFW window and Vulkan surface creation
- [x] Dynamic rendering path with `VK_KHR_dynamic_rendering`
- [x] Graphics, present, and compute queue-family selection
- [x] Render-target wrapper for sampled color and depth images
- [x] Per-frame camera/model/view/projection uniform buffers
- [x] Persistent descriptor set for render targets, samplers, textures, SSAO noise, and SSAO kernels
- [x] Per-frame descriptor sets for frame uniform data
- [x] Sponza glTF scene loading with `tinygltf`
- [x] glTF image and texture upload path
- [x] Standalone texture loading with `stb_image`
- [x] Mesh remapping during load with `meshoptimizer`
- [x] Camera helper in `foundation/`
- [x] CMake shader compilation to SPIR-V with `glslc`
- [x] Double-buffered frame resources
- [x] Manual image layout transitions for attachment, shader-read, general, and present layouts

## Implemented Passes

- [x] G-buffer pass using `gbuffer.vert` and `gbuffer.frag`
- [x] G-buffer normal/specular target: `R16G16B16A16_SFLOAT`
- [x] G-buffer albedo target: `R8G8B8A8_SNORM`
- [x] Depth target: `D32_SFLOAT`
- [x] Fullscreen triangle shader for post passes: `fullscreen.vert`
- [x] SSAO pass using `ssao.frag`
- [x] SSAO kernel buffer with 32 samples
- [x] SSAO noise texture
- [x] SSAO output target: `R32_SFLOAT`
- [x] SSAO blur target allocation
- [x] SSAO blur compute dispatch path
- [x] Final composition pass using `compose.frag`
- [x] Swapchain present step

## Scaffolded Or Not Wired Yet

- [ ] SSAO blur math in `ssao_blur.comp`
- [ ] Triangle filtering runtime pass using `filtering.comp`
- [ ] Filtered triangle buffer used by the draw call
- [ ] Shadow depth pass
- [ ] Deferred lighting pass
- [ ] Material texture sampling in the G-buffer shader

## Ambitious Roadmap

- [ ] Triangle filtering compute pass
- [ ] GPU frustum culling pass
- [ ] GPU occlusion culling with a depth pyramid
- [ ] GPU indirect draw command buffer
- [ ] Meshlet generation with `meshoptimizer`
- [ ] Meshlet culling pass
- [ ] Runtime LOD selection
- [ ] Particle system 
- [ ] Particle collision against depth
- [ ] GPU emitter buffers
- [ ] Shadow map pass
- [ ] Point light shadow cubemaps
- [ ] Deferred lighting pass
- [ ] Tiled or clustered lighting
- [ ] PBR material shader
- [ ] glTF base-color texture sampling
- [ ] Normal map sampling
- [ ] Metallic/roughness texture sampling
- [ ] Emissive material pass
- [ ] Alpha-tested material pass
- [ ] Transparent material pass
- [ ] Weighted blended transparency
- [ ] Bilateral SSAO blur
- [ ] SSAO settings for radius, strength, and bias
- [ ] Bloom prefilter/downsample/upsample passes
- [ ] HDR scene color target
- [ ] Exposure control
- [ ] Tone mapping pass
- [ ] FXAA pass
- [ ] Temporal anti-aliasing pass
- [ ] Skybox pass
- [ ] Image-based lighting
- [ ] Screen-space reflections
- [ ] Volumetric fog pass
- [ ] Debug views for albedo, normals, depth, SSAO, and lighting
- [ ] Render graph for pass ordering and resource tracking
- [ ] Shader hot reload
- [ ] ImGui debug panel
- [ ] GPU timestamp profiler
- [ ] Asset manifest for scenes, meshes, materials, and textures
- [ ] Material cache
- [ ] Texture cache
- [ ] Async asset loading jobs
- [ ] Screenshot capture
- [ ] Fly camera input controls
- [ ] README screenshots

## Build And Run

### Requirements

- CMake 3.20 or newer
- C++20 compiler
- Vulkan SDK or Vulkan development packages
- `glslc`
- Development packages for `assimp`, `SDL2`, `glm`, and `glfw3`
- Vulkan-capable GPU and driver
- Checked-in `external/` assets and third-party folders

### Configure And Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
```

The build creates `vkml` and compiles shaders into `build/shaders/*.spv`.

### Run

Run from inside the build directory because the executable loads shaders from `shaders/*.spv` and assets from `../external/...`.

```bash
cd build
./vkml
```

### Release Build

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j
cd build-release
./vkml
```

### Useful Targets

```bash
cmake --build build --target shaders
cmake --build build --target vkml
cmake --build build --target clean
```

## Troubleshooting

- Missing CMake package: install the matching development package or set `CMAKE_PREFIX_PATH`.
- Missing `shaders/*.spv`: build the project and run from the build directory.
- Missing `../external/sponza/glTF/Sponza.gltf`: check the asset folder and run from the build directory.
- Shader compiler error: check that `glslc` is installed and visible to CMake.

## Extra Docs

- Detailed shader and pipeline notes: `.docs/SHADER_AND_VULKAN_COMPONENTS.md`
- Main renderer source: `src/demo/main.cpp`
- Shader source: `shaders/`
- Camera helper: `foundation/`
