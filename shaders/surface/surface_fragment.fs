// Fragment stage declarations shared by all surface materials (color variants only):
// varyings, getters, the MaterialInputs structure and prepareMaterial().
//
// Expects from the material prolog:
//   SHADING_MODEL_LIT, MATERIAL_HAS_NORMAL (the material writes material.normal)

#include "common_math.glsl"
#include "common_brdf.glsl"
#include "common_uniforms.glsl"

#define VARYING in
#include "common_varyings.glsl"

layout(location = 0) out vec4 outColor;

//------------------------------------------------------------------------------
// Getters
//------------------------------------------------------------------------------

vec2 getUV0() {
    return fragUV;
}

vec3 getColor() {
    return fragColor;
}

vec3 getWorldPosition() {
    return fragPosWorld;
}

vec3 getWorldGeometricNormal() {
    return normalize(fragNormalWorld);
}

//------------------------------------------------------------------------------
// Shading parameters, valid after prepareMaterial()
//------------------------------------------------------------------------------

mat3 shading_tangentToWorld;
vec3 shading_view;
vec3 shading_normal;
vec3 shading_reflected;
float shading_NoV;

//------------------------------------------------------------------------------
// Material inputs
//------------------------------------------------------------------------------

struct MaterialInputs {
    vec4 baseColor;
#if defined(SHADING_MODEL_LIT)
    float roughness;
    float metallic;
    float reflectance;
    float ambientOcclusion;
#endif
#if defined(MATERIAL_HAS_NORMAL)
    vec3 normal;    // tangent space
#endif
};

void initMaterial(out MaterialInputs material) {
    material.baseColor = vec4(1.0);
#if defined(SHADING_MODEL_LIT)
    material.roughness = 1.0;
    material.metallic = 0.0;
    material.reflectance = 0.5;
    material.ambientOcclusion = 1.0;
#endif
#if defined(MATERIAL_HAS_NORMAL)
    material.normal = vec3(0.0, 0.0, 1.0);
#endif
}

// Called before material(): sets up the parts of the shading state that don't depend on the material.
void computeShadingParams() {
    vec3 n = fragNormalWorld;
    vec3 t = fragTangentWorld.xyz;
    vec3 b = cross(n, t) * sign(fragTangentWorld.w);

    shading_tangentToWorld = mat3(t, b, n);

    vec3 sv = view.projection[2].w != 0.0 ? // is perspective projection?
            (view.viewInverse[3].xyz - fragPosWorld) : view.viewInverse[2].xyz;

    shading_view = normalize(sv);
}

// Must be called by material(). material.normal only has an effect when written before this call.
void prepareMaterial(const MaterialInputs material) {
#if defined(MATERIAL_HAS_NORMAL)
    shading_normal = normalize(shading_tangentToWorld * material.normal);
#else
    shading_normal = getWorldGeometricNormal();
#endif
    shading_NoV = clampNoV(dot(shading_normal, shading_view));
    shading_reflected = reflect(-shading_view, shading_normal);
}
