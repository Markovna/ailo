#version 450
// Temporary stand-in for the vertex shader matcomp will generate from materials/lit.mat.
// Variants (CMakeLists.txt): VARIANT_HAS_SKINNING, VARIANT_DEPTH.

#include "lit_material.glsl"
#include "surface/surface_vertex.vs"

#if !defined(VARIANT_DEPTH)
void materialVertex(inout MaterialVertexInputs material) {
}
#endif

#include "surface/surface_main.vs"
