#version 450
#include "common_uniforms.glsl"

layout(location = 0) in vec3 inPosition;
#if defined(VARIANT_SKINNING)
layout(location = 5) in ivec4 inBoneIndices;
layout(location = 6) in vec4  inBoneWeights;
#endif

#define OBJECT_SKINNING_ENABLED_BIT 1

vec3 getBonePosition(vec3 pos, uint boneIdx) {
    return (bones[boneIdx].transform * vec4(pos, 1.0)).xyz;
}

vec4 getPosition() {
    vec4 position = vec4(inPosition, 1.0);
#if defined(VARIANT_SKINNING)
    if ((object.flags & OBJECT_SKINNING_ENABLED_BIT) != 0) {
        vec3 p = position.xyz;
        position.xyz = inBoneWeights.x * getBonePosition(p, uint(inBoneIndices.x))
                     + inBoneWeights.y * getBonePosition(p, uint(inBoneIndices.y))
                     + inBoneWeights.z * getBonePosition(p, uint(inBoneIndices.z))
                     + inBoneWeights.w * getBonePosition(p, uint(inBoneIndices.w));
    }
#endif
    return position;
}

void main() {
    gl_Position = view.projection * view.view * object.model * getPosition();
}
