#pragma once
#include "RenderPrimitive.h"

#include "common/AABB.h"

namespace ailo {

struct Mesh {
    struct Face {
        uint32_t indexOffset;
        uint32_t indexCount;
    };

    VertexBuffer vertexBuffer;
    BufferObject indexBuffer;
    std::vector<Face> faces;
    Aabb bounds {};
};

}
