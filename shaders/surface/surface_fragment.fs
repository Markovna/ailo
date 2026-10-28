// MATERIAL_HAS_NORMAL: defined by matc when the material writes material.normal.

#include "common_math.glsl"
#include "common_brdf.glsl"
#include "common_uniforms.glsl"

#define VARYING in
#include "surface/surface_varyings.glsl"

layout(location = 0) out vec4 outColor;

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

// A tangent-space normal from a normal map texel. texc stores normal maps as x, y in RG (BC5 has no blue channel),
// so z is rebuilt; this also holds for uncompressed maps that do store it.
vec3 unpackNormalMap(vec4 texel) {
    vec2 xy = texel.rg * 2.0 - 1.0;
    return vec3(xy, sqrt(max(1.0 - dot(xy, xy), 0.0)));
}

// Valid after prepareMaterial().
mat3 shading_tangentToWorld;
vec3 shading_view;
vec3 shading_normal;
vec3 shading_reflected;
float shading_NoV;

struct MaterialInputs {
    vec4 baseColor;
#if defined(SHADING_MODEL_LIT)
    float roughness;
    float metallic;
    float reflectance;
    float ambientOcclusion;
    vec3 emissive;  // linear color added after lighting
#endif
#if defined(MATERIAL_HAS_NORMAL)
    vec3 normal;    // tangent space
#endif
};

#if defined(MATERIAL_HAS_CUSTOM_SURFACE_SHADING)
// Arguments of the material's surfaceShading().
struct ShadingData {
    vec3 diffuseColor;
    vec3 f0;
    float perceptualRoughness;
    float roughness;
};

struct LightData {
    vec4 colorIntensity;
    vec3 l;
    float NdotL;
    vec3 worldPosition;
    float attenuation;
    float visibility;
};
#endif

void initMaterial(out MaterialInputs material) {
    material.baseColor = vec4(1.0);
#if defined(SHADING_MODEL_LIT)
    material.roughness = 1.0;
    material.metallic = 0.0;
    material.reflectance = 0.5;
    material.ambientOcclusion = 1.0;
    material.emissive = vec3(0.0);
#endif
#if defined(MATERIAL_HAS_NORMAL)
    material.normal = vec3(0.0, 0.0, 1.0);
#endif
}

void computeShadingParams() {
    vec3 n = fragNormalWorld;
    vec3 t = fragTangentWorld.xyz;
    vec3 b = cross(n, t) * sign(fragTangentWorld.w);

    shading_tangentToWorld = mat3(t, b, n);

    vec3 sv = view.projection[2].w != 0.0 ? // is perspective projection?
            (view.viewInverse[3].xyz - fragPosWorld) : view.viewInverse[2].xyz;

    shading_view = normalize(sv);
}

// material.normal only has an effect when written before this call.
void prepareMaterial(const MaterialInputs material) {
#if defined(MATERIAL_HAS_NORMAL)
    shading_normal = normalize(shading_tangentToWorld * material.normal);
#else
    shading_normal = getWorldGeometricNormal();
#endif
    shading_NoV = clampNoV(dot(shading_normal, shading_view));
    shading_reflected = reflect(-shading_view, shading_normal);
}
