#include "Skin.h"

#include "Engine.h"
#include "RenderAPI.h"
#include "Renderer.h"

namespace ailo {

Skin::Skin(Engine& engine)
    : m_buffer(std::make_shared<BufferObject>(engine, BufferBinding::UNIFORM, sizeof(BonesUniform)))
{ }

Skin::Skin(std::shared_ptr<BufferObject> sharedBuffer)
    : m_buffer(std::move(sharedBuffer))
{ }

}
