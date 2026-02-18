#version 450
#include "common_math.glsl"
#include "common_brdf.glsl"
#include "common_uniforms.glsl"

#define VARYING out
#include "common_varyings.glsl"

void main() {
    vec2 vertices[3] = vec2[3](
        vec2(-1.0, -1.0),
        vec2(3.0, -1.0),
        vec2(-1.0, 3.0)
    );

    gl_Position = vec4(vertices[gl_VertexIndex], 0.0, 1.0);
    fragUV = 0.5 * gl_Position.xy + vec2(0.5);
}