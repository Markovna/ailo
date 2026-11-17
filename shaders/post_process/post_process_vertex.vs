#include "common_math.glsl"
#include "common_uniforms.glsl"

#define VARYING out
#include "post_process/post_process_varyings.glsl"

float getExposure() {
    return view.exposure;
}

struct PostProcessVertexInputs {
    vec4 position;      // clip space
    vec2 normalizedUV;  // (0, 0) at the top-left of the target
#if defined(VARIABLE_CUSTOM0)
    vec4 VARIABLE_CUSTOM0;
#endif
#if defined(VARIABLE_CUSTOM1)
    vec4 VARIABLE_CUSTOM1;
#endif
#if defined(VARIABLE_CUSTOM2)
    vec4 VARIABLE_CUSTOM2;
#endif
#if defined(VARIABLE_CUSTOM3)
    vec4 VARIABLE_CUSTOM3;
#endif
};

// A single triangle covering the target: vertices 0, 1, 2 map to uv (0, 0), (2, 0), (0, 2).
void initPostProcessVertex(out PostProcessVertexInputs postProcess) {
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    postProcess.normalizedUV = uv;
    postProcess.position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
#if defined(VARIABLE_CUSTOM0)
    postProcess.VARIABLE_CUSTOM0 = vec4(0.0);
#endif
#if defined(VARIABLE_CUSTOM1)
    postProcess.VARIABLE_CUSTOM1 = vec4(0.0);
#endif
#if defined(VARIABLE_CUSTOM2)
    postProcess.VARIABLE_CUSTOM2 = vec4(0.0);
#endif
#if defined(VARIABLE_CUSTOM3)
    postProcess.VARIABLE_CUSTOM3 = vec4(0.0);
#endif
}
