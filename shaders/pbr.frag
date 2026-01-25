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

mat3 shading_tangentToWorld;
vec3 shading_view;
vec3 shading_normal;
float shading_NoV;

struct Light {
    vec4 colorIntensity;
    vec3 l;
    float NoL;
    vec3 position;
    float attenuation;
    vec3 direction;
};

struct Pixel {
    vec3 f0;
    float reflectance;
    vec3 diffuseColor;
    float perceptualRoughness;
    vec3 baseColor;
    float metallic;
    float roughness;
};

void getPixel(out Pixel pixel) {
    vec3 baseColor = texture(baseColorMap, fragUV).rgb;

    vec3 metallicRoughness = texture(metallicRoughnessMap, fragUV).rgb;
    float metallic = metallicRoughness.b;
    float roughness = metallicRoughness.g;

    pixel.baseColor = baseColor;
    pixel.perceptualRoughness = roughness;
    pixel.roughness = roughness * roughness;
    pixel.metallic = metallic;

    pixel.diffuseColor = baseColor.rgb * (1.0 - metallic);

    const float reflectance = 0.5;
    pixel.reflectance = 0.16 * reflectance * reflectance;

    pixel.f0 = mix(vec3(pixel.reflectance), baseColor.rgb, metallic);
}

vec3 specularLobe(vec3 f0, float roughness, vec3 h, float NoV, float NoL, float NoH, float LoH) {
    float D = distribution(roughness, NoH, shading_normal, h);
    float V = visibility(roughness, NoV, NoL);
    vec3 F = fresnel(f0, LoH);

    return (D * V) * F;
}

vec3 surfaceShading(const Pixel pixel, const Light light, float occlusion) {
    vec3 h = normalize(shading_view + light.l);
    //float NoV = max(abs(shading_NoV), 1e-5);
    float NoV = max(shading_NoV, 1e-5);
    float NoL = light.NoL;
    float NoH = clamp01(dot(shading_normal, h));
    float LoH = clamp01(dot(light.l, h));

    vec3 Fr = specularLobe(pixel.f0, pixel.roughness, h, NoV, NoL, NoH, LoH);
    vec3 Fd = pixel.diffuseColor * diffuse(pixel.roughness, NoV, NoL, LoH);

    vec3 color = Fd + Fr;// * pixel.energyCompensation;
    return (color * light.colorIntensity.rgb) *
                (light.colorIntensity.w * light.attenuation * NoL * occlusion);
}

float getSquareFalloffAttenuation(float distanceSquare, float falloff) {
    float factor = distanceSquare * falloff;
    float smoothFactor = clamp01(1.0 - factor * factor);
    // We would normally divide by the square distance here
    // but we do it at the call site
    return smoothFactor * smoothFactor;
}

float getDistanceAttenuation(const vec3 posToLight, const vec3 posToCamera, float falloff) {
    float distanceSquare = dot(posToLight, posToLight);
    float attenuation = getSquareFalloffAttenuation(distanceSquare, falloff);

    // light far attenuation
    // attenuation *= clamp01(view.lightFarAttenuationParams.x - dot(posToCamera, posToCamera) * view.lightFarAttenuationParams.y);
    return attenuation / max(distanceSquare, 1e-4);
}

float getAngleAttenuation(const vec3 lightDir, const vec3 l, const vec2 scaleOffset) {
     float cd = dot(lightDir, l);
     float attenuation = clamp01(cd * scaleOffset.x + scaleOffset.y);
     return attenuation * attenuation;
 }

Light getLight(int index) {
    vec3 position = lights[index].positionFalloff.xyz;
    float falloff = lights[index].positionFalloff.w;
    vec3 direction = lights[index].direction;
    vec2 scaleOffset = lights[index].scaleOffset;
    vec4 colorIntensity = lights[index].colorIntensity;

    vec3 posToLight = position - fragPosWorld;
    vec3 posToCamera = view.viewInverse[3].xyz - fragPosWorld;

    Light light;
    light.colorIntensity = colorIntensity;
    light.l = normalize(posToLight);
    light.NoL = clamp01(dot(shading_normal, light.l));
    light.attenuation = getDistanceAttenuation(posToLight, posToCamera, falloff);
    light.position = position;
    light.direction = direction;

    if(lights[index].type == SPOT_LIGHT_TYPE) {
        light.attenuation *= getAngleAttenuation(-direction, light.l, scaleOffset);
    }

    return light;
}

Light getDirectionalLight() {
    Light light;
    light.colorIntensity = view.lightColorIntensity;
    light.l = normalize(view.lightDirection);
    light.NoL = clamp01(dot(shading_normal, light.l));
    light.attenuation = 1.0;
    return light;
}

vec3 blingPhong(const Light light) {
    vec3 h = normalize(shading_view + light.l);
    float NoL = light.NoL;
    float NoH = clamp01(dot(shading_normal, h));

    vec3 diffuseLight = light.colorIntensity.rgb * light.colorIntensity.w * NoL;

    const float blinnPhongExponent = 128.0;
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
            (view.viewInverse[3].xyz - fragPosWorld) : view.viewInverse[2].xyz;

    shading_view = normalize(sv);

#if defined(USE_NORMAL_MAP)
    vec3 normal = texture(normalMap, fragUV).rgb;
    normal = normal * 2.0 - 1.0;
    shading_normal = normalize(shading_tangentToWorld * normal);
#else
    shading_normal = normalize(fragNormalWorld);
#endif

    shading_NoV = dot(shading_normal, shading_view);

    Pixel pixel;
    getPixel(pixel);

    vec3 color = vec3(0.0);
    Light directionalLight = getDirectionalLight();
    color += surfaceShading(pixel, directionalLight, 1.0);

    for(int i = 0; i < DYNAMIC_LIGHTS_COUNT; i++) {
        Light light = getLight(i);
        color += surfaceShading(pixel, light, 1.0);
    }

    vec3 ambient = vec3(0.02) * pixel.baseColor;
    color += ambient;

    outColor = vec4(color, 1.0);
}