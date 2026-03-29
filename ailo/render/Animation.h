#pragma once
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace ailo {

struct KeyVec3 {
    float time;
    glm::vec3 value;
};

struct KeyQuat {
    float time;
    glm::quat value;
};

struct BoneChannel {
    std::string boneName;
    std::vector<KeyVec3> positionKeys;
    std::vector<KeyQuat> rotationKeys;
    std::vector<KeyVec3> scaleKeys;
};

struct AnimationClip {
    std::string name;
    float duration;        // seconds
    float ticksPerSecond;
    std::vector<BoneChannel> channels;
};

} // namespace ailo
