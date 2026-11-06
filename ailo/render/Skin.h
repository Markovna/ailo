#pragma once
#include <cstdint>
#include <vector>
#include <glm/glm.hpp>

namespace ailo {

// Binds a mesh to a skeleton: the mesh's vertices index these bones. Only works with skeletons that have the node order
// of the one it was compiled against.
struct Skin {
    std::vector<uint32_t> nodeIndices;      // skin bone -> Skeleton::nodes
    std::vector<glm::mat4> inverseBinds;    // same order
};

}
