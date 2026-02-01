#pragma once
#include "RenderAPI.h"
#include "RenderPrimitive.h"
#include <memory>

#include "ecs/Scene.h"

namespace ailo {

class Engine;

struct Mesh {
    std::shared_ptr<VertexBuffer> vertexBuffer;
    std::shared_ptr<BufferObject> indexBuffer;
    std::vector<RenderPrimitive> primitives;
};

class MeshReader {
public:
    static std::shared_ptr<VertexBuffer> getCubeVertexBuffer(Engine&);
    static std::shared_ptr<BufferObject> getCubeIndexBuffer(Engine&);
    static Mesh createCubeMesh(Engine&);

    std::vector<Entity> read(Engine&, Scene&, const std::string& path);
};

}
