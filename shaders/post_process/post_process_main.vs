void main() {
    PostProcessVertexInputs postProcess;
    initPostProcessVertex(postProcess);
    postProcessVertex(postProcess);

    vertex_uv = postProcess.normalizedUV;
#if defined(VARIABLE_CUSTOM0)
    VARIABLE_CUSTOM_AT0 = postProcess.VARIABLE_CUSTOM0;
#endif
#if defined(VARIABLE_CUSTOM1)
    VARIABLE_CUSTOM_AT1 = postProcess.VARIABLE_CUSTOM1;
#endif
#if defined(VARIABLE_CUSTOM2)
    VARIABLE_CUSTOM_AT2 = postProcess.VARIABLE_CUSTOM2;
#endif
#if defined(VARIABLE_CUSTOM3)
    VARIABLE_CUSTOM_AT3 = postProcess.VARIABLE_CUSTOM3;
#endif

    gl_Position = postProcess.position;
}
