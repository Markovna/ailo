#version 450

layout (set = 0, binding = 0, std140) uniform perView {
   mat4 projection;
   mat4 view;
};

layout (set = 1, binding = 0, std140) uniform perObject {
   mat4 model;
};

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;

void main() {
    gl_Position = projection * view * model * vec4(inPosition, 1.0);
    fragColor = inColor;
    fragTexCoord = inTexCoord;
}
