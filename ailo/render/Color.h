#pragma once

#include "glm/vec4.hpp"

namespace ailo {

struct Color : glm::vec4 {
    using glm::vec4::vec4;
    constexpr Color(const glm::vec4& v) : glm::vec4(v) {}
};

}
