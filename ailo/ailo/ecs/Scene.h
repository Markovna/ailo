#pragma once

#include "ECS.h"

namespace ailo {

class Scene {
 public:
  ECS& getRegistry() { return m_ecs; }

 private:
  ECS m_ecs;
};

}