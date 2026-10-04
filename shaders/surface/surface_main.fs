void main() {
    computeShadingParams();

    MaterialInputs inputs;
    initMaterial(inputs);
    material(inputs);

    outColor = evaluateMaterial(inputs);
}
