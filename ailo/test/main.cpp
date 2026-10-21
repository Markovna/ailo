#include <iostream>

void runAppTests();
void runAssetServerTests();
void runEmbeddedAssetTests();
void runDiTests();
void runTextureTests();
void runModelTests();
void runLightingTests();
void runTransformTests();
void runPhysicsTests();

int main() {
    runAppTests();
    runAssetServerTests();
    runEmbeddedAssetTests();
    runDiTests();
    runTextureTests();
    runModelTests();
    runLightingTests();
    runTransformTests();
    runPhysicsTests();

    std::cout << "All ailo tests passed" << std::endl;
    return 0;
}
