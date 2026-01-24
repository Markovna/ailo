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

void main() {
    vec4 position = object.model * vec4(inPosition, 1.0);

    fragPosWorld = position.xyz;

    fragNormalWorld = normalize(mat3(object.modelInverseTranspose) * inNormal);
    fragTangentWorld.xyz = normalize(mat3(object.modelInverseTranspose) * inTangent.xyz);
    fragTangentWorld.w = inTangent.w;

    fragColor = inColor;
    fragUV = inUV;

    position = view.projection * view.view * position;

    gl_Position = position;
}