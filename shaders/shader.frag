#version 450

layout (set = 0, binding = 0, std140) uniform perView {
   mat4 projection;
   mat4 view;
   mat4 viewInverse;
   vec3 lightDirection;
   vec4 lightColorIntensity;
   vec4 ambientLightColorIntensity;
} view;

layout (set = 0, binding = 1, std140) uniform perLight {
    vec4 positionRadius;
    vec4 colorIntensity;
} lights;

layout(set = 2, binding = 0) uniform sampler2D texSampler;

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragPosWorld;
layout(location = 3) in vec3 fragNormalWorld;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 directionToLight = normalize(view.lightDirection);
    vec3 diffuseLight = view.lightColorIntensity.rgb * view.lightColorIntensity.w * max(dot(normalize(fragNormalWorld), directionToLight), 0.0);

    vec3 directionToDynamicLight = lights.positionRadius.xyz - fragPosWorld;
    float attenuation = lights.positionRadius.w / dot(directionToDynamicLight, directionToDynamicLight);
    vec3 dynamicLightColor = lights.colorIntensity.rgb * lights.colorIntensity.w * attenuation;
    vec3 dynamicDiffuseLight = dynamicLightColor * max(dot(normalize(fragNormalWorld), normalize(directionToDynamicLight)), 0.0);

    vec3 ambientLightColor = view.ambientLightColorIntensity.rgb * view.ambientLightColorIntensity.w;

    outColor = vec4((diffuseLight + dynamicDiffuseLight + ambientLightColor) * fragColor, 1.0);
}
