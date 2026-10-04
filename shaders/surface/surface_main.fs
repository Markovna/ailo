// Fragment shader entry point for surface materials (color variants).
// Included after the material's material() function and the shading model.

void main() {
    computeShadingParams();

    MaterialInputs inputs;
    initMaterial(inputs);
    material(inputs);

    outColor = evaluateMaterial(inputs);
}
