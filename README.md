# ailo

ailo is a game engine with physically based renderer written in C++23 on Vulkan. It has a plugin-based architecture built on an ECS (EnTT): engine features and games are plugins that add their own resources and systems. 

## Features

- **Rendering**: metallic/roughness PBR with normal maps, image-based lighting (prefiltered radiance cubemap and spherical-harmonics irradiance) and HDR tonemapping.
- **Lights and shadows**: one directional light with cascaded shadow maps, plus dynamic point and spot lights.
- **Materials**: material files declare parameters, render state and shading functions, and are compiled offline into packages holding every shader variant (skinning, shadows, depth-only).
- **Offline asset pipeline**: a Ninja-driven build compiles textures (BC-compressed with full mip chains), models (vertex-cache and overdraw optimized), image-based lighting and materials. Custom tools can be added for new asset types.
- **Assets at runtime**: ref-counted asset handles, loaders picked by file extension, and multiple asset sources (files on disk, embedded memory).
- **ECS and app framework**: EnTT scene, typed resources, systems scheduled in stages (including a fixed-step update), and a transform hierarchy.
- **Animation**: skeletal skinning and animation playback.
- **Physics**: Jolt Physics integration with static, kinematic and dynamic bodies and interpolated rendering.
- **Tooling**: Dear ImGui overlay.

## Samples

The samples live in `samples/`.

- `sandbox`: a demo scene with Sponza, an animated character and physics cubes, drawn with PBR materials.
- `asteroids`: a 3D Asteroids game.

## Building

See [BUILDING.md](BUILDING.md). CMake, a C++23 compiler, Python 3 and Ninja are required; every other dependency is vendored in `third_party/`.

## License

Apache License 2.0. See [LICENSE](LICENSE).
