#pragma once
#include "RenderPrimitive.h"
#include "common/AssetPool.h"
#include <memory>

namespace ailo {

struct Skin {
public:
    explicit Skin(Engine&);
    explicit Skin(std::shared_ptr<BufferObject> sharedBuffer);
    BufferObject& getBuffer() { return *m_buffer; }

private:
    std::shared_ptr<BufferObject> m_buffer;
};

}
