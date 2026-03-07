#pragma once
#include "RenderAPI.h"
#include "RenderPrimitive.h"
#include <memory>

#include "ecs/Scene.h"

namespace ailo {

class Engine;

struct Mesh : public enable_asset_ptr<Mesh> {
    std::shared_ptr<VertexBuffer> vertexBuffer;
    std::shared_ptr<BufferObject> indexBuffer;
    std::vector<RenderPrimitive> primitives;

    static asset_ptr<Mesh> cube(Engine&);
};

class MeshReader {
public:
    static std::vector<Entity> instantiate(Engine&, Scene&, const std::string& path);
};

}
