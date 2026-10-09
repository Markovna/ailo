#pragma once

#include "assets/AssetServer.h"

namespace ailo {

class DefaultAssets;
class RenderAPI;
class Texture;
struct Mesh;

namespace textures {

inline constexpr const char* kWhite = "builtin://textures/white";
inline constexpr const char* kBlack = "builtin://textures/black";
inline constexpr const char* kBlackCube = "builtin://textures/black_cube";
inline constexpr const char* kNormal = "builtin://textures/normal";
inline constexpr const char* kDefaultMetallicRoughness = "builtin://textures/default_metallic_roughness";

void createDefaultAssets(DefaultAssets&, AssetStorage<Texture>&, RenderAPI&);

}

namespace meshes {

inline constexpr const char* kSkyboxCube = "builtin://meshes/cube";
inline constexpr const char* kUnitCube = "builtin://meshes/unit_cube";

void createDefaultAssets(DefaultAssets&, AssetStorage<Mesh>&, RenderAPI&);

}

}
