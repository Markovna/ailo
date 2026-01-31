#version 450
#include "common_uniforms.glsl"

#define VARYING in
#include "common_varyings.glsl"

layout(set = 2, binding = 0) uniform samplerCube skybox;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(texture(skybox, fragPosWorld).rgb, 1.0);
}