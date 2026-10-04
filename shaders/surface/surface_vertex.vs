#include "common_math.glsl"
#include "common_uniforms.glsl"

// Locations match VertexLocation in render/RenderPrimitive.h.
layout(location = 0) in vec3 inPosition;

#if !defined(VARIANT_DEPTH)
#if defined(HAS_ATTRIBUTE_COLOR)
layout(location = 1) in vec3 inColor;
#endif
#if defined(HAS_ATTRIBUTE_UV0)
layout(location = 2) in vec2 inUV;
#endif
layout(location = 3) in vec3 inNormal;
#if defined(HAS_ATTRIBUTE_TANGENTS)
layout(location = 4) in vec4 inTangent;
#endif
#endif

#if defined(VARIANT_HAS_SKINNING)
layout(location = 5) in ivec4 inBoneIndices;
layout(location = 6) in vec4 inBoneWeights;
#endif

#if !defined(VARIANT_DEPTH)
#define VARYING out
#include "surface/surface_varyings.glsl"
#endif

#define OBJECT_SKINNING_ENABLED_BIT 1

#if defined(VARIANT_HAS_SKINNING)
bool isSkinningEnabled() {
    return (object.flags & OBJECT_SKINNING_ENABLED_BIT) != 0;
}

vec3 getBonePosition(vec3 pos, uint boneIdx) {
    return (bones[boneIdx].transform * vec4(pos, 1.0)).xyz;
}

vec3 getBoneVector(vec3 v, uint boneIdx) {
    return mat3(bones[boneIdx].transform) * v;
}

vec3 skinPosition(vec3 p) {
    return inBoneWeights.x * getBonePosition(p, uint(inBoneIndices.x))
         + inBoneWeights.y * getBonePosition(p, uint(inBoneIndices.y))
         + inBoneWeights.z * getBonePosition(p, uint(inBoneIndices.z))
         + inBoneWeights.w * getBonePosition(p, uint(inBoneIndices.w));
}

vec3 skinVector(vec3 v) {
    return inBoneWeights.x * getBoneVector(v, uint(inBoneIndices.x))
         + inBoneWeights.y * getBoneVector(v, uint(inBoneIndices.y))
         + inBoneWeights.z * getBoneVector(v, uint(inBoneIndices.z))
         + inBoneWeights.w * getBoneVector(v, uint(inBoneIndices.w));
}
#endif

// Object space, after skinning.
vec4 getPosition() {
    vec4 position = vec4(inPosition, 1.0);
#if defined(VARIANT_HAS_SKINNING)
    if (isSkinningEnabled()) {
        position.xyz = skinPosition(position.xyz);
    }
#endif
    return position;
}

mat4 getWorldFromModelMatrix() {
    return object.model;
}

mat4 getClipFromWorldMatrix() {
    return view.projection * view.view;
}

#if !defined(VARIANT_DEPTH)
struct MaterialVertexInputs {
#if defined(HAS_ATTRIBUTE_COLOR)
    vec3 color;
#endif
#if defined(HAS_ATTRIBUTE_UV0)
    vec2 uv0;
#endif
    vec3 worldNormal;
    vec4 worldPosition;
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

// Not part of MaterialVertexInputs: materials can't change the tangent frame.
vec4 vertex_worldTangent;

void initMaterialVertex(out MaterialVertexInputs material) {
    vec3 localNormal = inNormal;
#if defined(HAS_ATTRIBUTE_TANGENTS)
    vec3 localTangent = inTangent.xyz;
#else
    vec3 localTangent = vec3(1.0, 0.0, 0.0);
#endif

#if defined(VARIANT_HAS_SKINNING)
    if (isSkinningEnabled()) {
        localNormal = skinVector(localNormal);
        localTangent = skinVector(localTangent);
    }
#endif

    mat3 normalToWorld = mat3(object.modelInverseTranspose);
    material.worldNormal = normalize(normalToWorld * localNormal);
    vertex_worldTangent.xyz = normalize(normalToWorld * localTangent);
#if defined(HAS_ATTRIBUTE_TANGENTS)
    vertex_worldTangent.w = inTangent.w;
#else
    vertex_worldTangent.w = 1.0;
#endif

    material.worldPosition = getWorldFromModelMatrix() * getPosition();

#if defined(HAS_ATTRIBUTE_COLOR)
    material.color = inColor;
#endif
#if defined(HAS_ATTRIBUTE_UV0)
    material.uv0 = inUV;
#endif

#if defined(VARIABLE_CUSTOM0)
    material.VARIABLE_CUSTOM0 = vec4(0.0);
#endif
#if defined(VARIABLE_CUSTOM1)
    material.VARIABLE_CUSTOM1 = vec4(0.0);
#endif
#if defined(VARIABLE_CUSTOM2)
    material.VARIABLE_CUSTOM2 = vec4(0.0);
#endif
#if defined(VARIABLE_CUSTOM3)
    material.VARIABLE_CUSTOM3 = vec4(0.0);
#endif
}
#endif
