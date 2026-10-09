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
};

}
