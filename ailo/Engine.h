#pragma once

#include <memory>

namespace ailo {

struct RenderAPI;
struct Renderer;
struct InputSystem;

class Engine {
 public:
  Engine();
  ~Engine();

  Renderer* getRenderer();
  RenderAPI* getRenderAPI();
  InputSystem* getInputSystem();

 private:
  std::unique_ptr<Renderer> m_renderer;
  std::unique_ptr<RenderAPI> m_renderAPI;
  std::unique_ptr<InputSystem> m_inputSystem;
};

}