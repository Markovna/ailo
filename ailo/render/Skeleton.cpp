#include "Skeleton.h"

#include <cassert>
#include <glm/gtc/matrix_transform.hpp>

namespace ailo {

void Skeleton::updateModelPose() {
    modelPose.resize(nodes.size());
    for (size_t i = 0; i < nodes.size(); ++i) {
        const auto& node = pose[i];
        const glm::mat4 local = glm::translate(glm::mat4(1.0f), node.position)
                              * glm::mat4_cast(node.rotation)
                              * glm::scale(glm::mat4(1.0f), node.scale);

        const int parent = nodes[i].parentIndex;
        assert(parent < static_cast<int>(i));
        modelPose[i] = parent >= 0 ? modelPose[parent] * local : local;
    }
}

bool computeBoneMatrices(const Skeleton& skeleton, const Skin& skin, std::span<glm::mat4> out) {
    assert(out.size() <= skin.nodeIndices.size() && out.size() <= skin.inverseBinds.size());

    bool allFound = true;
    for (size_t i = 0; i < out.size(); ++i) {
        const uint32_t node = skin.nodeIndices[i];
        if (node < skeleton.modelPose.size()) {
            out[i] = skeleton.modelPose[node] * skin.inverseBinds[i];
        } else {
            out[i] = glm::mat4(1.0f);
            allFound = false;
        }
    }
    return allFound;
}

} // namespace ailo
