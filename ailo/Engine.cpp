#include "Engine.h"

#include "ecs/Scene.h"
#include "render/RenderAPI.h"
#include "render/Renderer.h"
#include "input/InputSystem.h"
#include "assets/Assets.h"
#include "render/Texture.h"

namespace ailo {

Engine::Engine(Platform::WindowHandle window) :
  m_renderAPI(std::make_unique<RenderAPI>(window)),
  m_assetManager(std::make_unique<AssetManager>()),
  m_inputSystem(std::make_unique<InputSystem>()) {

  m_assetManager->registerLoader<Texture>(std::make_unique<TextureLoader>(m_renderAPI.get()));

  m_renderer = std::make_unique<Renderer>(m_renderAPI.get(), m_assetManager.get());
}

Engine::~Engine() {
  m_renderer->terminate();
  m_assetManager->reset();
  m_renderAPI->shutdown();

  m_renderer.reset();
  m_assetManager.reset();
  m_renderAPI.reset();
  m_inputSystem.reset();
}

void Engine::gc() {
  m_assetManager->gc();
}

Renderer* Engine::getRenderer() { return m_renderer.get(); }
RenderAPI* Engine::getRenderAPI() { return m_renderAPI.get(); }
InputSystem* Engine::getInputSystem() { return m_inputSystem.get(); }
AssetManager* Engine::getAssetManager() { return m_assetManager.get(); }

std::unique_ptr<Scene> Engine::createScene() {
  auto scene = std::make_unique<Scene>();
  m_renderer->onSceneCreated(*scene);
  return scene;
}

}
