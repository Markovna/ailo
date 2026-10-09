#pragma once

#include <vector>

#include "Material.h"
#include "Mesh.h"
#include "Texture.h"
#include "assets/AssetServer.h"

namespace ailo {

class DefaultAssets {
public:
    void add(AssetPtr<Texture> texture) { m_textures.push_back(std::move(texture)); }
    void add(AssetPtr<Material> material) { m_materials.push_back(std::move(material)); }
    void add(AssetPtr<Mesh> mesh) { m_meshes.push_back(std::move(mesh)); }

private:
    std::vector<AssetPtr<Texture>> m_textures;
    std::vector<AssetPtr<Material>> m_materials;
    std::vector<AssetPtr<Mesh>> m_meshes;
};

}
