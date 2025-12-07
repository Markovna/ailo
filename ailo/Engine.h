#pragma once

#include <memory>

namespace ailo {

struct RenderAPI;
struct Renderer;
struct InputSystem;
struct Scene;
struct Camera;

class Engine {
 public:
  Engine();
  ~Engine();

  void render(Scene&, Camera&);

  Renderer* getRenderer();
  RenderAPI* getRenderAPI();
  InputSystem* getInputSystem();

 private:
  std::unique_ptr<Renderer> m_renderer;
  std::unique_ptr<RenderAPI> m_renderAPI;
  std::unique_ptr<InputSystem> m_inputSystem;
};

}