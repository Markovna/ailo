// Vertex shader entry point for surface materials. Included after the material's
// materialVertex() function (not called by depth variants).

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

    gl_Position = view.projection * view.view * material.worldPosition;
#endif
}
