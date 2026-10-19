#include "AssetPlugin.h"

#include "MemoryAssetReader.h"
#include "embedded/materials.h"

namespace ailo {

void AssetPlugin::build(App& app) {
    auto& server = app.insertResource<AssetServer>(std::make_unique<FileAssetReader>(root));
    server.registerReader("engine", std::make_unique<MemoryAssetReader>(MemoryAssetReader {
        { "materials/lit.matpack", embedded::materials::kMaterialsLit },
        { "materials/skybox.matpack", embedded::materials::kMaterialsSkybox },
    }));
}

}
