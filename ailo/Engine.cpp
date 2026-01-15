#include "Engine.h"

#include "ecs/Scene.h"
#include "render/RenderAPI.h"
#include "render/Renderer.h"
#include "input/InputSystem.h"

namespace ailo {

Engine::Engine(GLFWwindow* window) :
  m_renderAPI(std::make_unique<RenderAPI>(window)),
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

Renderer* Engine::getRenderer() { return m_renderer.get(); }
RenderAPI* Engine::getRenderAPI() { return m_renderAPI.get(); }
InputSystem* Engine::getInputSystem() { return m_inputSystem.get(); }

std::unique_ptr<Scene> Engine::createScene() const {
  return std::make_unique<Scene>();
}

std::shared_ptr<Shader> Engine::loadShader(const ShaderDescription& description) {
  return std::shared_ptr<Shader>(
          new Shader(*this, description),
          [this](Shader* p) {
              p->destroy(*this);
              delete p;
          });
}

}
