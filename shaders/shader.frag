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

const float blinnPhongExponent = 512.0;

vec3 pointLight(vec3 lightPos, float radius, vec4 color, vec3 surfaceNormal, vec3 viewDir) {
    vec3 directionToLight = lightPos - fragPosWorld;
    float attenuation = radius / dot(directionToLight, directionToLight);
    directionToLight = normalize(directionToLight);
    vec3 lightColor = color.rgb * color.w * attenuation;
    vec3 diffuseLight = lightColor * max(dot(surfaceNormal, directionToLight), 0.0);
    vec3 halfAngle = normalize(directionToLight + viewDir);
    float blinnTerm = pow(clamp(dot(surfaceNormal, halfAngle), 0, 1), blinnPhongExponent);
    vec3 specularLight = lightColor * blinnTerm;
    return diffuseLight + specularLight;
}

vec3 directionalLight(vec3 lightDirection, vec4 lightColorIntensity, vec3 surfaceNormal, vec3 viewDir) {
    vec3 directionToLight = normalize(lightDirection);
    vec3 diffuseLight = lightColorIntensity.rgb * lightColorIntensity.w * max(dot(surfaceNormal, directionToLight), 0.0);

    vec3 halfAngle = normalize(directionToLight + viewDir);
    float blinnTerm = pow(clamp(dot(surfaceNormal, halfAngle), 0, 1), blinnPhongExponent);
    vec3 specularLight = diffuseLight * blinnTerm;
    return diffuseLight + specularLight;
}

void main() {
    vec3 surfaceNormal = normalize(fragNormalWorld);
    vec3 cameraPosWorld = view.viewInverse[3].xyz;
    vec3 viewDir = normalize(cameraPosWorld - fragPosWorld);

    vec3 directionalLight = directionalLight(view.lightDirection, view.lightColorIntensity, surfaceNormal, viewDir);
    vec3 pointLight = pointLight(lights.positionRadius.xyz, lights.positionRadius.w, lights.colorIntensity, surfaceNormal, viewDir);

    vec3 ambientLight = view.ambientLightColorIntensity.rgb * view.ambientLightColorIntensity.w;

    outColor = vec4((ambientLight + directionalLight + pointLight) * fragColor, 1.0);
}
