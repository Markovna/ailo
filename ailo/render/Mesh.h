#pragma once
#include "RenderAPI.h"
#include "RenderPrimitive.h"

#include "assets/Assets.h"

namespace ailo {

struct Mesh : public Asset {
    struct Face {
        uint32_t indexOffset;
        uint32_t indexCount;
    };

    VertexBuffer vertexBuffer;
    BufferObject indexBuffer;
    std::vector<Face> faces;

    static asset_ptr<Mesh> cube(AssetManager* assetManager, RenderAPI* renderApi);
};

}
