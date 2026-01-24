#version 450
#include "common_math.glsl"
#include "common_brdf.glsl"
#include "common_uniforms.glsl"

#define VARYING in
#include "common_varyings.glsl"

layout(set = 2, binding = 0) uniform sampler2D baseColorMap;

#if defined(USE_NORMAL_MAP)
layout(set = 2, binding = 1) uniform sampler2D normalMap;
#endif

layout(set = 2, binding = 2) uniform sampler2D metallicRoughnessMap;

layout(location = 0) out vec4 outColor;

vec3 shading_view;
mat3 shading_tangentToWorld;
vec3 shading_normal;
float shading_NoV;

struct Light {
    vec4 colorIntensity;
    vec3 l;
    float NoL;
    float attenuation;
};

vec3 specularLobe(vec3 f0, float roughness, vec3 h, float NoV, float NoL, float NoH, float LoH) {
    float D = distribution(roughness, NoH, shading_normal, h);
    float V = visibility(roughness, NoV, NoL);
    vec3 F = fresnel(f0, LoH);

    return (D * V) * F;
}

vec3 surfaceShading(vec3 baseColor, float metallic, float perceptualRoughness, const Light light, float occlusion) {
    vec3 h = normalize(shading_view + light.l);
    float NoV = abs(shading_NoV) + 1e-5;
    float NoL = light.NoL;
    float NoH = clamp01(dot(shading_normal, h));
    float LoH = clamp01(dot(light.l, h));

    float reflectance = 0.5;
    reflectance = 0.16 * reflectance * reflectance;

    vec3 f0 = baseColor.rgb * metallic + reflectance * (1.0 - metallic);

    float roughness = perceptualRoughness * perceptualRoughness ;

    vec3 Fr = specularLobe(f0, roughness, h, NoV, NoL, NoH, LoH);

    vec3 diffuseColor = baseColor.rgb * (1.0 - metallic);
    vec3 Fd = diffuseColor * diffuse(roughness, NoV, NoL, LoH);

    vec3 color = Fd + Fr;// * energyCompensation;
    return (color * light.colorIntensity.rgb) *
                (light.colorIntensity.w * light.attenuation * NoL * occlusion);
}

Light getDirectionalLight() {
    Light light;
    light.colorIntensity = view.lightColorIntensity;
    light.l = normalize(view.lightDirection);
    light.NoL = clamp01(dot(shading_normal, light.l));
    light.attenuation = 1.0;
    return light;
}

vec3 directionToRgb(vec3 dir) {
    return vec3(0.5 + 0.5 * dir.xyz);
}

const float blinnPhongExponent = 128.0;

vec3 blingPhong(const Light light) {
    vec3 h = normalize(shading_view + light.l);
    float NoL = light.NoL;
    float NoH = clamp01(dot(shading_normal, h));

    vec3 diffuseLight = light.colorIntensity.rgb * light.colorIntensity.w * NoL;

    float blinnTerm = pow(NoH, blinnPhongExponent);
    vec3 specularLight = diffuseLight * blinnTerm;
    return diffuseLight + specularLight;
}

void main() {
    vec3 n = fragNormalWorld;
    vec3 t = fragTangentWorld.xyz;
    vec3 b = cross(n, t) * sign(fragTangentWorld.w);

    shading_tangentToWorld = mat3(t, b, n);

    vec3 sv = view.projection[2].w != 0.0 ? // is perspective projection?
            (view.viewInverse[3].xyz - fragPosWorld) :
             view.viewInverse[2].xyz;

    shading_view = normalize(sv);

#if defined(USE_NORMAL_MAP)
    vec3 normal = texture(normalMap, fragUV).rgb;
    normal = normal * 2.0 - 1.0;
    shading_normal = normalize(shading_tangentToWorld * normal);
#else
    shading_normal = normalize(fragNormalWorld);
#endif

    shading_NoV = dot(shading_normal, shading_view);

    vec3 metallicRoughness = texture(metallicRoughnessMap, fragUV).rgb;
    float metallic = metallicRoughness.b;
    float roughness = metallicRoughness.g;

    vec3 baseColor = texture(baseColorMap, fragUV).rgb;

    baseColor = pow(baseColor, vec3(2.2));

    Light directionalLight = getDirectionalLight();

    vec3 color = surfaceShading(baseColor.rgb, metallic, roughness, directionalLight, 1.0);

    vec3 ambient = vec3(0.04) * baseColor;
    color += ambient;

    // HDR tonemapping
    // color = color / (color + vec3(1.0));

    baseColor = pow(baseColor, vec3(1.0 / 2.2));

    outColor = vec4(color, 1.0);
}