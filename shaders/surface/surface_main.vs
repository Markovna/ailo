void main() {
#if defined(VARIANT_DEPTH)
    gl_Position = getClipFromWorldMatrix() * getWorldFromModelMatrix() * getPosition();
#else
    MaterialVertexInputs material;
    initMaterialVertex(material);
    materialVertex(material);

    fragPosWorld = material.worldPosition.xyz;
    fragNormalWorld = material.worldNormal;
    fragTangentWorld = vertex_worldTangent;

#if defined(HAS_ATTRIBUTE_COLOR)
    fragColor = material.color;
#else
    fragColor = vec3(1.0);
#endif
#if defined(HAS_ATTRIBUTE_UV0)
    fragUV = material.uv0;
#else
    fragUV = vec2(0.0);
#endif

#if defined(VARIABLE_CUSTOM0)
    VARIABLE_CUSTOM_AT0 = material.VARIABLE_CUSTOM0;
#endif
#if defined(VARIABLE_CUSTOM1)
    VARIABLE_CUSTOM_AT1 = material.VARIABLE_CUSTOM1;
#endif
#if defined(VARIABLE_CUSTOM2)
    VARIABLE_CUSTOM_AT2 = material.VARIABLE_CUSTOM2;
#endif
#if defined(VARIABLE_CUSTOM3)
    VARIABLE_CUSTOM_AT3 = material.VARIABLE_CUSTOM3;
#endif

    gl_Position = view.projection * view.view * material.worldPosition;
#endif
}
