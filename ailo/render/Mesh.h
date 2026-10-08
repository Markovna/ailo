#pragma once
#include "RenderAPI.h"
#include "RenderPrimitive.h"

#include "assets/AssetServer.h"

namespace ailo {

struct Mesh {
    struct Face {
        uint32_t indexOffset;
        uint32_t indexCount;
    };

    VertexBuffer vertexBuffer;
    BufferObject indexBuffer;
    std::vector<Face> faces;

    static AssetPtr<Mesh> skyboxCube(AssetStorage<Mesh>& storage, RenderAPI* renderApi);
    static AssetPtr<Mesh> unitCube(AssetStorage<Mesh>& storage, RenderAPI* renderApi);
};

}
