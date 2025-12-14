#include "Engine.h"

#include "ecs/Scene.h"
#include "render/RenderAPI.h"
#include "render/Renderer.h"
#include "input/InputSystem.h"

namespace ailo {

Engine::Engine() :
  m_renderAPI(std::make_unique<RenderAPI>()),
  m_renderer(std::make_unique<Renderer>()),
  m_inputSystem(std::make_unique<InputSystem>())
{ }

Engine::~Engine() {
  m_renderer->terminate(*this);
  m_renderAPI->shutdown();

  m_renderer.reset();
  m_renderAPI.reset();
  m_inputSystem.reset();
}

void Engine::init(GLFWwindow* window) {
  m_renderAPI->init(window);
}

void Engine::render(Scene& scene, Camera& camera) {
  m_renderer->render(*this, scene, camera);
}

Renderer* Engine::getRenderer() { return m_renderer.get(); }
RenderAPI* Engine::getRenderAPI() { return m_renderAPI.get(); }
InputSystem* Engine::getInputSystem() { return m_inputSystem.get(); }

std::unique_ptr<Scene> Engine::createScene() const {
  return std::make_unique<Scene>();
}

}
