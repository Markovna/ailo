
struct ViewUniform {
   mat4 projection;
   mat4 view;
   mat4 viewInverse;
   vec3 lightDirection;
   vec4 lightColorIntensity;
   vec4 ambientLightColorIntensity;
};

struct LightUniform {
    vec4 positionRadius;
    vec4 colorIntensity;
    vec3 direction;
    uint type;
    vec2 scaleOffset;
};

struct ObjectUniform {
    mat4 model;
    mat4 modelInverse;
    mat4 modelInverseTranspose;
};

#define DYNAMIC_LIGHTS_COUNT 3

layout (set = 0, binding = 0, std140)
uniform perFrame_view {
    ViewUniform view;
};

layout (set = 0, binding = 1, std140)
uniform perFrame_lights {
    LightUniform lights[DYNAMIC_LIGHTS_COUNT];
};

layout (set = 1, binding = 0, std140)
uniform perObject {
    ObjectUniform object;
};