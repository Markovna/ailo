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

#define DYNAMIC_LIGHTS_COUNT 3
layout (set = 0, binding = 1, std140)
uniform perLight {
    LightUniform lights[DYNAMIC_LIGHTS_COUNT];
};

layout (set = 1, binding = 0, std140) uniform perObject {
   mat4 model;
   mat4 modelInverse;
   mat4 modelInverseTranspose;
} object;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inNormal;
layout(location = 4) in vec3 inTangent;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragPosWorld;
layout(location = 3) out vec3 fragNormalWorld;
layout(location = 4) out mat3 fragTBN;

void main() {
    vec4 worldPosition = object.model * vec4(inPosition, 1.0);
    gl_Position = view.projection * view.view * worldPosition;

    fragPosWorld = worldPosition.xyz;
    fragNormalWorld = normalize(mat3(object.modelInverseTranspose) * inNormal);
    fragColor = inColor;
    fragTexCoord = inTexCoord;

    // Calculate TBN matrix
    vec3 T = normalize(mat3(object.model) * inTangent);
    vec3 N = normalize(mat3(object.model) * inNormal);
    vec3 B = normalize(cross(N, T));
    fragTBN = mat3(T, B, N);
}
