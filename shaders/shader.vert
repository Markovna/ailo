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

layout (set = 1, binding = 0, std140) uniform perObject {
   mat4 model;
   mat4 modelInverse;
   mat4 modelInverseTranspose;
} object;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inNormal;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragPosWorld;
layout(location = 3) out vec3 fragNormalWorld;

void main() {
    vec4 worldPosition = object.model * vec4(inPosition, 1.0);
    gl_Position = view.projection * view.view * worldPosition;

    fragPosWorld = worldPosition.xyz;
    fragNormalWorld = normalize(mat3(object.modelInverseTranspose) * inNormal);
    fragColor = inColor;
    fragTexCoord = inTexCoord;
}
