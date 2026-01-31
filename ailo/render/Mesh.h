#pragma once
#include "RenderAPI.h"
#include "RenderPrimitive.h"
#include <memory>

#include "ecs/Scene.h"

namespace ailo {

class Engine;

struct Mesh {
    std::unique_ptr<VertexBuffer> vertexBuffer;
    std::unique_ptr<BufferObject> indexBuffer;
    std::vector<std::shared_ptr<Material>> materials;

    std::vector<RenderPrimitive> primitives;
};

class MeshReader {
public:

    static std::unique_ptr<VertexBuffer> getCubeVertexBuffer(Engine&);
    static std::unique_ptr<BufferObject> getCubeIndexBuffer(Engine&);

    std::vector<Entity> read(Engine&, Scene&, const std::string& path);
};

}
