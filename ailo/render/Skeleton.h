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
    glm::mat4 localTransform;  // from aiNode::mTransformation (bind-pose default)
    glm::mat4 inverseBindPose; // from aiBone::mOffsetMatrix; identity for non-bone nodes
    glm::mat4 worldTransform;
    int boneOutputIndex;        // index into BonesUniform::bones[]; -1 for non-bone nodes
};

class Skeleton {
public:
    // All nodes in parent-first (DFS) order: bones + their non-bone ancestors.
    std::vector<NodeInfo> nodes;
    std::unordered_map<std::string, uint32_t> nodeNameToIndex;

    // Evaluates animation at `time` and writes final skinning matrices into out.
    // Only nodes with boneOutputIndex >= 0 write to out.bones[].
    void updateBoneTransforms(float time, const AnimationClip& clip, BonesUniform& out);

private:
};

} // namespace ailo
