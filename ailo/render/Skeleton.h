#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>
#include "Animation.h"
#include "Renderer.h"

namespace ailo {

// Represents any node in the scene hierarchy that is relevant to skinning:
// either an actual bone or a non-bone intermediate ancestor of bones.
struct NodeInfo {
    std::string name;
    int parentIndex;           // parent in Skeleton::nodes[], -1 for root
    int boneOutputIndex;        // index into BonesUniform::bones[]; -1 for non-bone nodes
};

class Skeleton {
public:
    // All nodes in parent-first (DFS) order: bones + their non-bone ancestors.
    std::vector<NodeInfo> nodes;
    std::vector<NodeTransform> bindPoseTransforms; // bindPose decomposed, so clips can be blended per component
    std::vector<glm::mat4> bindPose;
    std::vector<glm::mat4> inverseBindPose;
    std::unordered_map<std::string, uint32_t> nodeNameToIndex;
};

} // namespace ailo
