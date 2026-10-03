#pragma once

#include <Jolt/Jolt.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

// glm <-> Jolt conversions. Note the component order: glm::quat(w, x, y, z) vs JPH::Quat(x, y, z, w).
namespace ailo::physics {

inline JPH::Vec3 toJolt(const glm::vec3& v) { return { v.x, v.y, v.z }; }
inline JPH::RVec3 toJoltR(const glm::vec3& v) { return { v.x, v.y, v.z }; }
inline JPH::Quat toJolt(const glm::quat& q) { return { q.x, q.y, q.z, q.w }; }

inline glm::vec3 toGlm(JPH::Vec3Arg v) { return { v.GetX(), v.GetY(), v.GetZ() }; }
#ifdef JPH_DOUBLE_PRECISION
inline glm::vec3 toGlm(JPH::DVec3Arg v) {
    return { static_cast<float>(v.GetX()), static_cast<float>(v.GetY()), static_cast<float>(v.GetZ()) };
}
#endif
inline glm::quat toGlm(JPH::QuatArg q) { return { q.GetW(), q.GetX(), q.GetY(), q.GetZ() }; }

}
