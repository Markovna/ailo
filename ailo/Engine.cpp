#include "Engine.h"

#include "ecs/Scene.h"
#include "render/RenderAPI.h"
#include "render/Renderer.h"
#include "input/InputSystem.h"
#include "common/AssetPool.h"

namespace ailo {

Engine::Engine(Platform::WindowHandle window) :
  m_renderAPI(std::make_unique<RenderAPI>(window)),
  m_assetManager(std::make_unique<AssetManager>()),
  m_renderer(std::make_unique<Renderer>(*this)),
  m_inputSystem(std::make_unique<InputSystem>())
{ }

Engine::~Engine() {
  m_renderer->terminate(*this);
  m_assetManager->reset(*this);
  m_renderAPI->shutdown();

  m_renderer.reset();
  m_assetManager.reset();
  m_renderAPI.reset();
  m_inputSystem.reset();
}

void Engine::gc() {
  m_assetManager->gc(*this);
}

Renderer* Engine::getRenderer() { return m_renderer.get(); }
RenderAPI* Engine::getRenderAPI() { return m_renderAPI.get(); }
InputSystem* Engine::getInputSystem() { return m_inputSystem.get(); }
AssetManager* Engine::getAssetManager() { return m_assetManager.get(); }

std::unique_ptr<Scene> Engine::createScene() {
  auto scene = std::make_unique<Scene>();
  m_renderer->onSceneCreated(*this, *scene);
  return scene;
}

}
