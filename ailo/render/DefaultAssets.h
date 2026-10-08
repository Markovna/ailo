#pragma once

#include <vector>

#include "Texture.h"
#include "assets/AssetServer.h"

namespace ailo {

inline constexpr const char* kWhiteTexture = "builtin://textures/white";
inline constexpr const char* kBlackTexture = "builtin://textures/black";
inline constexpr const char* kBlackCubeTexture = "builtin://textures/black_cube";
inline constexpr const char* kNormalTexture = "builtin://textures/normal@norm";
inline constexpr const char* kDefaultMetallicRoughnessTexture = "builtin://textures/default_metallic_roughness";

// Keeps the built-in assets alive: while this resource exists they can be loaded by the keys above.
class DefaultAssets {
public:
    DefaultAssets(RenderAPI*, AssetStorage<Texture>&);

private:
    std::vector<AssetPtr<Texture>> m_textures;
};

}
