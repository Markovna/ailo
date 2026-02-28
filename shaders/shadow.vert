#version 450
#include "common_uniforms.glsl"

layout(location = 0) in vec3 inPosition;

void main() {
    gl_Position = view.projection * view.view * object.model * vec4(inPosition, 1.0);
}
