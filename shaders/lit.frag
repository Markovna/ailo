#version 450
// Temporary stand-in for the fragment shader matcomp will generate from materials/lit.mat.
// Variants (CMakeLists.txt): VARIANT_HAS_SHADOWING, VARIANT_DEPTH.

#include "lit_material.glsl"

#if defined(VARIANT_DEPTH)

#include "surface/surface_depth_main.fs"

#else

#include "surface/surface_fragment.fs"

void material(inout MaterialInputs material) {
    material.normal = texture(materialParams_normalMap, getUV0()).rgb * 2.0 - 1.0;
    prepareMaterial(material);

    material.baseColor = texture(materialParams_baseColorMap, getUV0());
    vec3 metallicRoughness = texture(materialParams_metallicRoughnessMap, getUV0()).rgb;
    material.metallic = metallicRoughness.b;
    material.roughness = metallicRoughness.g;
}

#include "surface/surface_shading_lit.fs"
#include "surface/surface_main.fs"

#endif
