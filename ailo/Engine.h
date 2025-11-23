#pragma once

#include <memory>

namespace ailo {

struct RenderAPI;
struct InputSystem;

class Engine {
 public:
  Engine();
  ~Engine();

  RenderAPI* getRenderAPI();
  InputSystem* getInputSystem();

 private:
  std::unique_ptr<RenderAPI> m_renderAPI;
  std::unique_ptr<InputSystem> m_inputSystem;
};

}