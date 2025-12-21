#pragma once
#include "RenderAPI.h"
#include "RenderPrimitive.h"
#include <glm/glm.hpp>
#include <string>

#include "ecs/Scene.h"

namespace ailo {

class Engine;

struct Mesh {
    std::unique_ptr<BufferObject> vertexBuffer;
    std::unique_ptr<BufferObject> indexBuffer;
    std::vector<std::unique_ptr<Material>> materials;

    std::vector<RenderPrimitive> primitives;
};

class MeshReader {
public:
    std::vector<Entity> read(Engine&, Scene&, const std::string& path);
};

}
