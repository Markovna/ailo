void main() {
    PostProcessInputs inputs;
    initPostProcess(inputs);
    postProcess(inputs);

    outColor = inputs.color;
}
