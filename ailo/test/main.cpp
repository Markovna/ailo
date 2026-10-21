#include "Testing.h"

void runAppTests();
void runAssetServerTests();
void runEmbeddedAssetTests();
void runTextureTests();
void runModelTests();
void runLightingTests();
void runTransformTests();
void runPhysicsTests();

int main() {
    runAppTests();
    runAssetServerTests();
    runEmbeddedAssetTests();
    runTextureTests();
    runModelTests();
    runLightingTests();
    runTransformTests();
    runPhysicsTests();

    return ailo::test::finish();
}
