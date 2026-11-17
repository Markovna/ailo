#include "common_math.glsl"
#include "common_uniforms.glsl"

#define VARYING in
#include "post_process/post_process_varyings.glsl"

layout(location = 0) out vec4 outColor;

vec2 getUV() {
    return vertex_uv;
}

float getExposure() {
    return view.exposure;
}

struct PostProcessInputs {
    vec4 color;
};

void initPostProcess(out PostProcessInputs postProcess) {
    postProcess.color = vec4(0.0);
}
