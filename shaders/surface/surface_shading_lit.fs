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
    vec4 baseColor;
    float metallic;
    float roughness;
//    vec3 energyCompensation;
    vec3 dfg;
};

#define MIN_PERCEPTUAL_ROUGHNESS 0.045

vec3 sampleDFG(float NoV, float perceptualRoughness) {
    // The LUT sampler repeats: keep the lookup inside the edge texel centers so
    // bilinear filtering never blends in the opposite edge of the table.
    vec2 halfTexel = 0.5 / vec2(textureSize(iblDFG, 0));
    vec2 uv = clamp(vec2(NoV, perceptualRoughness), halfTexel, 1.0 - halfTexel);
    return textureLod(iblDFG, uv, 0.0).rgb;
}

// Diffuse irradiance / pi in direction n, from the L2 SH baked by lightc. The basis order and constants must match
// tools/lightc/src/SphericalHarmonics.cpp. Clamped because a truncated SH of a high-contrast environment rings negative.
vec3 irradianceSH(vec3 n) {
    return max(
          view.iblSH[0].rgb
        + view.iblSH[1].rgb * n.y
        + view.iblSH[2].rgb * n.z
        + view.iblSH[3].rgb * n.x
        + view.iblSH[4].rgb * (n.y * n.x)
        + view.iblSH[5].rgb * (n.y * n.z)
        + view.iblSH[6].rgb * (3.0 * n.z * n.z - 1.0)
        + view.iblSH[7].rgb * (n.z * n.x)
        + view.iblSH[8].rgb * (n.x * n.x - n.y * n.y),
        0.0);
}

void getPixel(const MaterialInputs material, out Pixel pixel) {
    vec4 baseColor = material.baseColor;
    float metallic = material.metallic;
    // At roughness 0, D_GGX is a delta and punctual lights would produce no highlight at all.
    float roughness = clamp(material.roughness, MIN_PERCEPTUAL_ROUGHNESS, 1.0);

    pixel.baseColor = baseColor;
    pixel.perceptualRoughness = roughness;
    pixel.roughness = roughness * roughness;
    pixel.metallic = metallic;

    pixel.diffuseColor = baseColor.rgb * (1.0 - metallic);

    float reflectance = material.reflectance;
    pixel.reflectance = 0.16 * reflectance * reflectance;

    pixel.f0 = mix(vec3(pixel.reflectance), baseColor.rgb, metallic);
    pixel.dfg = sampleDFG(shading_NoV, pixel.perceptualRoughness);
    //pixel.energyCompensation = 1.0 + pixel.f0 * (1.0 / pixel.dfg.y - 1.0);
}

vec3 specularLobe(vec3 f0, float roughness, vec3 h, float NoV, float NoL, float NoH, float LoH) {
    float D = distribution(roughness, NoH, h);
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
    light.position = vec3(0.0);
    light.direction = -light.l;
    return light;
}

#if defined(VARIANT_HAS_SHADOWING)
float sampleShadow(vec2 uv, float depth, vec4 tileRect) {
    // Keeps the 2x2 footprint of the comparison filter inside the cascade's atlas tile.
    uv = clamp(uv, tileRect.xy, tileRect.zw);
    // Explicit LOD: the shadow map has no mips, and this avoids implicit derivatives in non-uniform control flow.
    return textureLod(shadowMap, vec3(uv, depth), 0.0);
}

// Normal offset (in shadow map texels) along the geometric normal, scaled by sin of the angle to the light.
#define SHADOW_NORMAL_OFFSET 1.0
// Small constant depth bias in world units; the normal offset handles the slope-dependent part.
#define SHADOW_DEPTH_BIAS 0.02

float sampleCascade(int cascade, vec3 worldPos, vec3 geometricNormal, float NoL) {
    vec2 atlasSize = vec2(textureSize(shadowMap, 0));
    vec2 texelSize = 1.0 / atlasSize;
    vec4 scaleOffset = shadows.cascades[cascade].atlasScaleOffset;
    float tileSize = atlasSize.x * scaleOffset.x;

    // Orthographic light projection: NDC x scale is 1 / half-extent, so one texel spans 2 / (size * scale) world units.
    mat4 lvp = shadows.cascades[cascade].lightFromWorldMatrix;
    float ndcScale = length(vec3(lvp[0][0], lvp[1][0], lvp[2][0]));
    float texelWorldSize = 2.0 / (tileSize * ndcScale);
    // Depth is mapped [near, far] -> [0, 1], so this is 1 / depth range: it keeps the bias constant in world units
    // whatever each cascade's depth range is.
    float depthScale = length(vec3(lvp[0][2], lvp[1][2], lvp[2][2]));
    float sinTheta = sqrt(max(1.0 - NoL * NoL, 0.0));
    worldPos += geometricNormal * (texelWorldSize * SHADOW_NORMAL_OFFSET * sinTheta);

    vec4 lightSpacePos = lvp * vec4(worldPos, 1.0);
    vec3 projCoords = lightSpacePos.xyz;

    vec2 shadowUV = projCoords.xy * 0.5 + 0.5;
    // Outside the light frustum: treat as lit instead of smearing edge texels.
    if (shadowUV != clamp01(shadowUV) || projCoords.z > 1.0) {
        return 1.0;
    }

    float depth = projCoords.z - SHADOW_DEPTH_BIAS * depthScale;

    vec2 atlasUV = shadowUV * scaleOffset.xy + scaleOffset.zw;
    vec4 tileRect = vec4(scaleOffset.zw + 0.5 * texelSize, scaleOffset.zw + scaleOffset.xy - 0.5 * texelSize);

    vec2 uv = atlasUV * atlasSize + 0.5;
    vec2 base = (floor(uv) - 0.5) * texelSize;
    vec2 st = fract(uv);

    vec2 uw = vec2(3.0 - 2.0 * st.x, 1.0 + 2.0 * st.x);
    vec2 vw = vec2(3.0 - 2.0 * st.y, 1.0 + 2.0 * st.y);

    vec2 u = vec2((2.0 - st.x) / uw.x - 1.0, st.x / uw.y + 1.0) * texelSize.x;
    vec2 v = vec2((2.0 - st.y) / vw.x - 1.0, st.y / vw.y + 1.0) * texelSize.y;

    float shadow = 0.0;
    shadow += uw.x * vw.x * sampleShadow(base + vec2(u.x, v.x), depth, tileRect);
    shadow += uw.y * vw.x * sampleShadow(base + vec2(u.y, v.x), depth, tileRect);
    shadow += uw.x * vw.y * sampleShadow(base + vec2(u.x, v.y), depth, tileRect);
    shadow += uw.y * vw.y * sampleShadow(base + vec2(u.y, v.y), depth, tileRect);
    return shadow * (1.0 / 16.0);
}

float getViewDepth(vec3 worldPos) {
    return -(view.view * vec4(worldPos, 1.0)).z;
}

// Returns cascadeCount past the last cascade.
int getShadowCascade(float viewDepth) {
    int count = int(shadows.cascadeCount);
    for (int i = 0; i < count; i++) {
        if (viewDepth < shadows.cascadeSplits[i]) {
            return i;
        }
    }
    return count;
}

float calculateShadow(vec3 worldPos, vec3 geometricNormal, float NoL) {
    float viewDepth = getViewDepth(worldPos);
    int cascade = getShadowCascade(viewDepth);
    int count = int(shadows.cascadeCount);
    if (cascade >= count) {
        return 1.0;
    }

    float visibility = sampleCascade(cascade, worldPos, geometricNormal, NoL);

    // Cross-fade into the next cascade (or out of shadow after the last one) to hide the seam.
    float splitNear = cascade == 0 ? 0.0 : shadows.cascadeSplits[cascade - 1];
    float splitFar = shadows.cascadeSplits[cascade];
    float blendStart = splitFar - (splitFar - splitNear) * shadows.cascadeBlend;
    if (viewDepth > blendStart) {
        float next = cascade + 1 < count ? sampleCascade(cascade + 1, worldPos, geometricNormal, NoL) : 1.0;
        visibility = mix(visibility, next, smoothstep(blendStart, splitFar, viewDepth));
    }
    return visibility;
}

vec3 getCascadeDebugColor(int cascade) {
    const vec3 colors[MAX_SHADOW_CASCADES + 1] = vec3[](
        vec3(1.0, 0.3, 0.3), vec3(0.3, 1.0, 0.3), vec3(0.3, 0.3, 1.0), vec3(1.0, 1.0, 0.3), vec3(1.0));
    return colors[cascade];
}
#endif

#if defined(MATERIAL_HAS_CUSTOM_SURFACE_SHADING)
// Called for every light, even when the fragment faces away from it or is in its shadow,
// so surfaceShading() can add its own ambient term.
vec3 customSurfaceShading(const MaterialInputs material, const Pixel pixel, const Light light, float visibility) {
    LightData lightData;
    lightData.colorIntensity = light.colorIntensity;
    lightData.l = light.l;
    lightData.NdotL = light.NoL;
    lightData.worldPosition = light.position;
    lightData.attenuation = light.attenuation;
    lightData.visibility = visibility;

    ShadingData shadingData;
    shadingData.diffuseColor = pixel.diffuseColor;
    shadingData.f0 = pixel.f0;
    shadingData.perceptualRoughness = pixel.perceptualRoughness;
    shadingData.roughness = pixel.roughness;

    return surfaceShading(material, shadingData, lightData);
}
#endif

vec4 evaluateMaterial(const MaterialInputs material) {
    Pixel pixel;
    getPixel(material, pixel);

    vec3 color = vec3(0.0);

    Light directionalLight = getDirectionalLight();
#if defined(VARIANT_HAS_SHADOWING)
    // Use the geometric normal for shadowing: normal-mapped NoL can be positive on faces turned away from the light.
    vec3 geometricNormal = getWorldGeometricNormal();
    float geometricNoL = dot(geometricNormal, directionalLight.l);
#if defined(MATERIAL_HAS_CUSTOM_SURFACE_SHADING)
    float visibility = 0.0;
    if (geometricNoL > 0.0 && directionalLight.NoL > 0.0) {
        visibility = calculateShadow(fragPosWorld, geometricNormal, geometricNoL);
    }
    color += customSurfaceShading(material, pixel, directionalLight, visibility);
#else
    if (geometricNoL > 0.0 && directionalLight.NoL > 0.0) {
        float shadow = calculateShadow(fragPosWorld, geometricNormal, geometricNoL);
        color += surfaceShading(pixel, directionalLight, shadow);
    }
#endif
#elif defined(MATERIAL_HAS_CUSTOM_SURFACE_SHADING)
    color += customSurfaceShading(material, pixel, directionalLight, 1.0);
#else
    color += surfaceShading(pixel, directionalLight, 1.0);
#endif

    int lightCount = min(int(view.lightCount), DYNAMIC_LIGHTS_COUNT);
    for(int i = 0; i < lightCount; i++) {
        Light light = getLight(i);
#if defined(MATERIAL_HAS_CUSTOM_SURFACE_SHADING)
        color += customSurfaceShading(material, pixel, light, 1.0);
#else
        color += surfaceShading(pixel, light, 1.0);
#endif
    }

    const float ambientLuminance = view.iblIntensity;

    vec3 E = mix(pixel.dfg.xxx, pixel.dfg.yyy, pixel.f0);

    vec3 reflected = mix(shading_reflected, shading_normal, pixel.roughness * pixel.roughness);
    float radianceLod = view.iblSpecularMaxLod * pixel.perceptualRoughness * (2.0 - pixel.perceptualRoughness);
    vec3 prefilteredRadiance = textureLod(iblSpecular, reflected, radianceLod).rgb;
    vec3 Fr = E * prefilteredRadiance;

    vec3 diffuseIrradiance = irradianceSH(shading_normal);
    vec3 Fd = pixel.diffuseColor * diffuseIrradiance * (1.0 - E) * material.ambientOcclusion;

    color.rgb += ambientLuminance * (Fd + Fr);

#if defined(VARIANT_HAS_SHADOWING)
    if ((shadows.shadowFlags & SHADOW_FLAG_VISUALIZE_CASCADES) != 0u) {
        color.rgb *= getCascadeDebugColor(getShadowCascade(getViewDepth(fragPosWorld)));
    }
#endif

    return vec4(color, pixel.baseColor.a);
}
