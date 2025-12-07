#include "Engine.h"

#include "render/RenderAPI.h"
#include "render/Renderer.h"
#include "input/InputSystem.h"

namespace ailo {

Engine::Engine() :
  m_inputSystem(std::make_unique<InputSystem>()),
  m_renderAPI(std::make_unique<RenderAPI>()),
  m_renderer(std::make_unique<Renderer>())
{ }

Engine::~Engine() {
  m_renderer->terminate(*this);

  m_renderer.reset();
  m_renderAPI.reset();
  m_inputSystem.reset();
}

void Engine::render(Scene& scene, Camera& camera) {
  m_renderer->render(*this, scene, camera);
}

Renderer* Engine::getRenderer() { return m_renderer.get(); }
RenderAPI* Engine::getRenderAPI() { return m_renderAPI.get(); }
InputSystem* Engine::getInputSystem() { return m_inputSystem.get(); }

}