#pragma once

#include <memory>

struct GLFWwindow;

namespace ailo {
struct ShaderDescription;

class RenderAPI;
class Renderer;
class InputSystem;
class Scene;
class Shader;
class Texture;
class Material;
class AssetManager;
struct Camera;

class Engine {
 public:
  Engine(GLFWwindow*);
  ~Engine();

 void gc();

  Renderer* getRenderer();
  RenderAPI* getRenderAPI();
  InputSystem* getInputSystem();
  AssetManager* getAssetManager();

  [[nodiscard]] std::unique_ptr<Scene> createScene();

 private:
  std::unique_ptr<RenderAPI> m_renderAPI;
  std::unique_ptr<AssetManager> m_assetManager;
  std::unique_ptr<Renderer> m_renderer;
  std::unique_ptr<InputSystem> m_inputSystem;
};

}