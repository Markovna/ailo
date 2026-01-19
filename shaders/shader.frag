#version 450

struct LightUniform {
    vec4 positionRadius;
    vec4 colorIntensity;
    vec3 direction;
    uint type;
    vec2 scaleOffset;
};

layout (set = 0, binding = 0, std140) uniform perView {
   mat4 projection;
   mat4 view;
   mat4 viewInverse;
   vec3 lightDirection;
   vec4 lightColorIntensity;
   vec4 ambientLightColorIntensity;
} view;

layout (set = 0, binding = 1, std140) uniform perLight {
    LightUniform light;
};

layout(set = 2, binding = 0) uniform sampler2D texSampler;
//layout(set = 2, binding = 1) uniform sampler2D normalMap;

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragPosWorld;
layout(location = 3) in vec3 fragNormalWorld;
layout(location = 4) in mat3 fragTBN;

layout(location = 0) out vec4 outColor;

// TODO: make this a uniform
const float blinnPhongExponent = 128.0;

const int POINT_LIGHT_TYPE = 0;
const int SPOT_LIGHT_TYPE = 1;

float getAngleAttenuation(vec3 lightDir, vec3 posToLight, vec2 scaleOffset) {
    float cd = dot(lightDir, normalize(posToLight));
    float attenuation = clamp(cd * scaleOffset.x + scaleOffset.y, 0, 1);
    return attenuation * attenuation;
}

vec3 pointLight(LightUniform light, vec3 surfaceNormal, vec3 viewDir) {
    vec3 lightPos = light.positionRadius.xyz;
    float radius = light.positionRadius.w;

    vec3 directionToLight = lightPos - fragPosWorld;
    float attenuation = radius / dot(directionToLight, directionToLight);

    if(light.type == SPOT_LIGHT_TYPE) {
        attenuation *= getAngleAttenuation(light.direction, directionToLight, light.scaleOffset);
    }

    directionToLight = normalize(directionToLight);
    vec3 lightColor = light.colorIntensity.rgb * light.colorIntensity.w * attenuation;
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
    // with normal map
    //vec3 normal = texture(normalMap, fragTexCoord).rgb;
    //normal = normalize(normal);
    //vec3 surfaceNormal = normalize(fragTBN * normal);

    // without normal map
    vec3 surfaceNormal = normalize(fragNormalWorld);

    vec3 cameraPosWorld = view.viewInverse[3].xyz;
    vec3 viewDir = normalize(cameraPosWorld - fragPosWorld);

    vec3 directionalLight = directionalLight(view.lightDirection, view.lightColorIntensity, surfaceNormal, viewDir);
    vec3 pointLight = pointLight(light, surfaceNormal, viewDir);

    vec3 ambientLight = view.ambientLightColorIntensity.rgb * view.ambientLightColorIntensity.w;

    vec3 texColor = texture(texSampler, fragTexCoord).rgb;
    outColor = vec4((ambientLight + directionalLight + pointLight) * fragColor * texColor, 1.0);
}
