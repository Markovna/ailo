#pragma once
#include "Scene.h"
#include "glm/mat4x4.hpp"

namespace ailo {

struct Transform {
  glm::mat4 transform = glm::mat4(1.0f); // world transform
};

}
