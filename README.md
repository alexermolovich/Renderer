# Roadmap

## Implemented Techniques

- [x] Vulkan swapchain and presentation
- [x] GLFW window and Vulkan surface
- [x] Dynamic rendering with `VK_KHR_dynamic_rendering`
- [x] Render-target abstraction
- [x] Sponza glTF scene loading
- [x] Texture loading and upload
- [x] Mesh remapping with `meshoptimizer`
- [x] Fly camera controls
- [x] G-buffer rendering
- [x] SSAO
- [x] SSAO blur pass
- [x] Cascaded shadow mapping
- [x] Shadow depth rendering
- [x] GPU triangle filtering
- [x] GPU frustum culling
- [x] Per-cascade triangle filtering
- [x] Composition pass with direct-light shading and cascade shadows
- [x] Screen-space reflections (SSR) pass blended in compose
- [x] FXAA
- [x] CMake shader compilation to SPIR-V

## Active work

- [ ] SSAO blur improvements
- [ ] SSR quality improvements (edge fades, temporal stability)

## Future Roadmap

- [ ] GPU occlusion culling with a depth pyramid
- [ ] Meshlet generation with `meshoptimizer`
- [ ] Meshlet culling pass
- [ ] Runtime LOD selection
- [ ] Particle system
- [ ] Particle collision against depth
- [ ] Point light shadow cubemaps
- [ ] Deferred lighting pass
- [ ] Tiled or clustered lighting
- [ ] PBR material shader
- [ ] Alpha-tested material pass
- [ ] Transparent material pass
- [ ] Bilateral SSAO blur
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
- [ ] README screenshots

## Build And Run

### Requirements

- CMake 3.20 or newer
- C++20 compiler
- Vulkan SDK or Vulkan development packages
- `glslc`
- Development packages for `assimp`, `SDL2`, `glm`, and `glfw3`
- Vulkan-capable GPU and driver
- Local `external/` assets and third-party folders (ignored by git)

### External Folder Notes

This repo now treats `external/` as local-only. Populate it with the dependencies/assets required by `CMakeLists.txt`:

- `external/imgui`
- `external/tracy`
- `external/tinygltf`
- `external/cglm`
- `external/enkiTS`
- `external/meshoptimizer`
- `external/gltf-sample-assets` (Sponza is loaded from this tree)

### Configure And Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
```

The build creates `vkml` and compiles shaders into `build/shaders/*.spv`.

### Run

Run from inside the build directory because the executable loads shaders from `shaders/*.spv`.

```bash
cd build
./vkml
```

Assets are resolved from the source tree via `VKML_SOURCE_DIR`, so launching from `build/` is sufficient as long as `external/` is populated.

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
