struct ViewUniform {
   mat4 projection;
   mat4 view;
   mat4 viewInverse;
   vec3 lightDirection;
   vec4 lightColorIntensity;
   vec4 ambientLightColorIntensity;

   float iblSpecularMaxLod;
   mat4 lightViewProjection;
};

struct LightUniform {
    vec4 positionFalloff;
    vec4 colorIntensity;
    vec3 direction;
    uint type;
    vec2 scaleOffset;
};

struct ObjectUniform {
    mat4 model;
    mat4 modelInverse;
    mat4 modelInverseTranspose;
    uint flags;
};

struct BoneUniform {
    mat4 transform;
};

#define DYNAMIC_LIGHTS_COUNT 2
#define MAX_BONES_COUNT 256

const int POINT_LIGHT_TYPE = 0;
const int SPOT_LIGHT_TYPE = 1;

layout (set = 0, binding = 0, std140)
uniform perFrame_view {
    ViewUniform view;
};

layout (set = 0, binding = 1, std140)
uniform perFrame_lights {
    LightUniform lights[DYNAMIC_LIGHTS_COUNT];
};

layout (set = 0, binding = 2)
uniform samplerCube iblSpecular;

layout (set = 0, binding = 3)
uniform sampler2D iblDFG;

layout (set = 0, binding = 4)
uniform sampler2D shadowMap;

layout (set = 1, binding = 0, std140)
uniform perObject {
    ObjectUniform object;
};

layout (set = 1, binding = 1, std140)
uniform perObject_bones {
    BoneUniform bones[MAX_BONES_COUNT];
};