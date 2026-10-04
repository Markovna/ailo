// Temporary stand-in for the material header matcomp will generate from materials/lit.mat:
//
//   "shadingModel": "lit",
//   "requires": ["color", "uv0", "tangents"],
//   "parameters": [ sampler2d baseColorMap, sampler2d normalMap, sampler2d metallicRoughnessMap ]

#define SHADING_MODEL_LIT
#define HAS_ATTRIBUTE_COLOR
#define HAS_ATTRIBUTE_UV0
#define HAS_ATTRIBUTE_TANGENTS
#define MATERIAL_HAS_NORMAL

// Bindings match the order Material::setTexture() is called with in Model.cpp and Spawn.cpp.
layout(set = 2, binding = 0) uniform sampler2D materialParams_baseColorMap;
layout(set = 2, binding = 1) uniform sampler2D materialParams_normalMap;
layout(set = 2, binding = 2) uniform sampler2D materialParams_metallicRoughnessMap;
