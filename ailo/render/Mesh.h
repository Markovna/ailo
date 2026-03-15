#pragma once
#include "RenderAPI.h"
#include "RenderPrimitive.h"
#include <memory>

#include "ecs/Scene.h"

namespace ailo {

class Engine;

struct Mesh : public enable_asset_ptr<Mesh> {
    struct Face {
        uint32_t indexOffset;
        uint32_t indexCount;
    };

    std::shared_ptr<VertexBuffer> vertexBuffer;
    std::shared_ptr<BufferObject> indexBuffer;
    std::vector<Face> faces;

    static asset_ptr<Mesh> cube(Engine&);
};

class MeshReader {
public:
    static std::vector<Entity> instantiate(Engine&, Scene&, const std::string& path);
};

}
