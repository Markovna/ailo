#include "Engine.h"

#include "render/RenderAPI.h"
#include "input/InputSystem.h"

namespace ailo {

Engine::Engine() :
  m_inputSystem(std::make_unique<InputSystem>()),
  m_renderAPI(std::make_unique<RenderAPI>())
{ }

RenderAPI* Engine::getRenderAPI() { return m_renderAPI.get(); }
InputSystem* Engine::getInputSystem() { return m_inputSystem.get(); }

}