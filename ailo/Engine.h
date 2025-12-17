#pragma once

#include <memory>

struct GLFWwindow;

namespace ailo {

class RenderAPI;
class Renderer;
class InputSystem;
class Scene;

struct Camera;

class Engine {
 public:
  Engine();
  ~Engine();

  void init(GLFWwindow*);
  void render(Scene&, Camera&);

  Renderer* getRenderer();
  RenderAPI* getRenderAPI();
  InputSystem* getInputSystem();

  std::unique_ptr<Scene> createScene() const;

 private:
  std::unique_ptr<RenderAPI> m_renderAPI;
  std::unique_ptr<Renderer> m_renderer;
  std::unique_ptr<InputSystem> m_inputSystem;
};

}