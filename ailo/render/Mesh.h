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
    static Mesh createCubeMesh(Engine&);
    static std::vector<Entity> instantiate(Engine&, Scene&, const std::string& path);
};

}
