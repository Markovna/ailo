#version 450

layout(set = 0, binding = 0) uniform sampler2D hdrColor;
layout(set = 0, binding = 1) uniform TonemapUniforms {
    float exposure;
} params;

layout(location = 0) out vec4 outColor;

const float kWhitePoint = 4.0;

vec3 reinhardExtended(vec3 x) {
    return min(x * (1.0 + x / (kWhitePoint * kWhitePoint)) / (1.0 + x), 1.0);
}

void main() {
    vec3 color = texelFetch(hdrColor, ivec2(gl_FragCoord.xy), 0).rgb * params.exposure;
    outColor = vec4(reinhardExtended(color), 1.0);
}
