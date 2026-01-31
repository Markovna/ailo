#version 450
#include "common_uniforms.glsl"

#define VARYING out
#include "common_varyings.glsl"

layout(location = 0) in vec3 inPos;

void main() {
    mat4 rotView = mat4(mat3(view.view));
    vec4 clipPos = view.projection * rotView * vec4(inPos, 1.0);

    fragPosWorld = inPos;

    gl_Position = clipPos.xyww;
}