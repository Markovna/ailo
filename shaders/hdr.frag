#version 450
#include "common_math.glsl"
#include "common_brdf.glsl"
#include "common_uniforms.glsl"

#define VARYING in
#include "common_varyings.glsl"

layout(set = 2, binding = 0) uniform sampler2D hdrTexture;

layout(location = 0) out vec4 outColor;

void main() {
    const float gamma = 2.2;
    vec3 color = texture(hdrTexture, fragUV).rgb;
    //color = pow(color, vec3(1.0 / gamma));
    outColor = vec4(color.rgb, 1.0);
}