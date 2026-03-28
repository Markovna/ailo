#version 450
#include "common_math.glsl"
#include "common_brdf.glsl"
#include "common_uniforms.glsl"

#define VARYING out
#include "common_varyings.glsl"

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec3 inNormal;
layout(location = 4) in vec4 inTangent;

#if defined(VARIANT_SKINNING)
layout(location = 5) in ivec4 inBoneIndices;
layout(location = 6) in vec4 inBoneWeights;
#endif

#define OBJECT_SKINNING_ENABLED_BIT 1

vec3 getBonePosition(vec3 pos, uint boneIdx) {
    return (bones[boneIdx].transform * vec4(pos, 1.0)).xyz;
}

vec4 getPosition() {
    vec4 position = object.model * vec4(inPosition, 1.0);

#if defined(VARIANT_SKINNING)
    if((object.flags & OBJECT_SKINNING_ENABLED_BIT) != 0) {
        vec3 pos = position.xyz;
        position.xyz = inBoneWeights.x * getBonePosition(pos, uint(inBoneIndices.x))
                        + inBoneWeights.y * getBonePosition(pos, uint(inBoneIndices.y))
                        + inBoneWeights.z * getBonePosition(pos, uint(inBoneIndices.z))
                        + inBoneWeights.w * getBonePosition(pos, uint(inBoneIndices.w));
    }
#endif

    return position;
}

void main() {
    vec4 position = getPosition();
    mat3 normalToWorld = mat3(object.modelInverseTranspose);

    fragPosWorld = position.xyz;
    fragNormalWorld = normalize(normalToWorld * inNormal);
    fragTangentWorld.xyz = normalize(normalToWorld * inTangent.xyz);
    fragTangentWorld.w = inTangent.w;

    fragColor = inColor;
    fragUV = inUV;

    gl_Position = view.projection * view.view * position;
}