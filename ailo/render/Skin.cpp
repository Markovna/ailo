#include "Skin.h"

#include "Engine.h"
#include "RenderAPI.h"
#include "Renderer.h"

namespace ailo {

Skin::Skin(RenderAPI* renderApi)
    : m_buffer(std::make_shared<BufferObject>(renderApi, BufferBinding::UNIFORM, sizeof(BonesUniform)))
{ }

Skin::Skin(std::shared_ptr<BufferObject> sharedBuffer)
    : m_buffer(std::move(sharedBuffer))
{ }

}
