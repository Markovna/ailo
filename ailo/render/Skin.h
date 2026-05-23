#pragma once
#include "RenderPrimitive.h"
#include <memory>

namespace ailo {

struct Skin {
public:
    explicit Skin(RenderAPI*);
    explicit Skin(std::shared_ptr<BufferObject> sharedBuffer);
    BufferObject& getBuffer() { return *m_buffer; }

private:
    std::shared_ptr<BufferObject> m_buffer;
};

}
