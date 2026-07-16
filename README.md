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
- [x] Shadow generation uniform buffer with cascade matrices, split depths, map size, and bias data
- [x] Persistent descriptor binding for the cascade shadow depth array
- [x] Persistent descriptor binding for the composed post-processing image
- [x] Sponza glTF scene loading with `tinygltf`
- [x] glTF image and texture upload path
- [x] Standalone texture loading with `stb_image`
- [x] Mesh remapping during load with `meshoptimizer`
- [x] Camera helper in `foundation/`
- [x] Fly camera movement and arrow-key look controls
- [x] CMake shader compilation to SPIR-V with `glslc`
- [x] Double-buffered frame resources
- [x] Manual image layout transitions for attachment, shader-read, general, and present layouts

## Implemented Passes

- [x] G-buffer pass using `gbuffer.vert` and `gbuffer.frag`
- [x] G-buffer normal/specular target: `R16G16B16A16_SFLOAT`
- [x] G-buffer albedo target: `R8G8B8A8_UNORM`
- [x] Depth target: `D32_SFLOAT`
- [x] Fullscreen triangle shader for post passes: `fullscreen.vert`
- [x] SSAO pass using `ssao.frag`
- [x] SSAO kernel buffer with 32 samples
- [x] SSAO noise texture
- [x] SSAO output target: `R32_SFLOAT`
- [x] SSAO blur target allocation
- [x] SSAO blur compute dispatch path
- [x] Cascade shadow-map depth target: 4-layer `D32_SFLOAT` array
- [x] Per-cascade shadow-map image views
- [x] Shadow depth pass using `shadow_depth.vert` and `shadow_depth.frag`
- [x] Cascade shadow-map sampling in `compose.frag`
- [x] World-space receiver reconstruction for cascade shadow lookup
- [x] 3x3 PCF filtering for cascade shadow sampling
- [x] Final composition pass using `compose.frag`
- [x] Compose pass output render target for post processing
- [x] FXAA pass using `fxaa_pass.frag`
- [x] FXAA pipeline rendering to the swapchain image
- [x] Swapchain present step

## Current Frame Order

- [x] Cascade shadow triangle filtering compute pass
- [x] Cascade shadow depth pass
- [x] G-buffer pass
- [x] SSAO pass
- [x] SSAO blur pass dispatch
- [x] Compose pass into the offscreen compose image
- [x] FXAA pass from compose image to swapchain image
- [x] Present swapchain image

## Active work

- [ ] SSAO blur math in `ssao_blur.comp`
- [x] Triangle filtering runtime pass using `filtering.comp`
- [x] Filtered triangle buffer used by the draw call
- [x] Cascade triangle filtering runtime pass using `filtering_cascade.comp`
- [x] Filtered triangle buffers and indirect draw commands for each shadow cascade
- [x] Shadow depth pass
- [ ] Deferred lighting pass
- [x] Material texture sampling in the G-buffer shader
- [x] Compose-image post-processing target bound through descriptor binding 12
- [x] Up/down camera movement on `E`/`Q` and `H`/`J`

## Future Roadmap

- [x] Triangle filtering compute pass
- [x] GPU frustum culling pass
- [ ] GPU occlusion culling with a depth pyramid
- [x] GPU indirect draw command buffer
- [ ] Meshlet generation with `meshoptimizer`
- [ ] Meshlet culling pass
- [ ] Runtime LOD selection
- [ ] Particle system
- [ ] Particle collision against depth
- [x] Shadow map pass
- [x] Cascade shadow mapping
- [ ] Point light shadow cubemaps
- [ ] Deferred lighting pass
- [ ] Tiled or clustered lighting
- [ ] PBR material shader
- [ ] Alpha-tested material pass
- [ ] Transparent material pass
- [ ] Bilateral SSAO blur
- [x] FXAA pass
- [ ] Temporal anti-aliasing pass
- [ ] Skybox pass
- [ ] Image-based lighting
- [ ] Screen-space reflections
- [ ] Volumetric fog pass
- [ ] Render graph for pass ordering and resource tracking
- [ ] Shader hot reload
- [ ] ImGui debug panel
- [ ] GPU timestamp profiler
- [ ] Asset manifest for scenes, meshes, materials, and textures
- [ ] Material cache
- [ ] Texture cache
- [ ] Async asset loading jobs
- [ ] Screenshot capture
- [x] Fly camera input controls
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

### Controls

- `W` / `S`: move forward and backward
- `A` / `D`: strafe left and right
- `E` / `Q`: move up and down
- `H` / `J`: move up and down
- Arrow keys: rotate camera yaw and pitch

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
