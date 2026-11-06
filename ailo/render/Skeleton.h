#pragma once
#include <span>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include "Animation.h"
#include "Skin.h"

namespace ailo {

struct NodeInfo {
    std::string name;
    int parentIndex;           // parent in Skeleton::nodes[], -1 for root
};

struct Skeleton {
    std::vector<NodeInfo> nodes; // parent-first order
    std::vector<NodeTransform> restPose;
    std::vector<NodeTransform> pose;
    std::vector<glm::mat4> modelPose;

    void updateModelPose();
};

// Writes the skinning matrices of the skin's first out.size() bones. A bone whose node the skeleton doesn't have gets
// identity. Returns false if there was such a bone.
bool computeBoneMatrices(const Skeleton& skeleton, const Skin& skin, std::span<glm::mat4> out);

} // namespace ailo
