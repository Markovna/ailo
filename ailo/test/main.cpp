#include "Testing.h"

void runAppTests();
void runAssetServerTests();
void runEmbeddedAssetTests();
void runTextureTests();
void runModelTests();
void runLightingTests();
void runMaterialInstanceTests();
void runTransformTests();
void runPhysicsTests();
void runAnimationStateMachineTests();
void runSkeletonTests();

int main() {
    runAppTests();
    runAssetServerTests();
    runEmbeddedAssetTests();
    runTextureTests();
    runModelTests();
    runLightingTests();
    runMaterialInstanceTests();
    runTransformTests();
    runPhysicsTests();
    runAnimationStateMachineTests();
    runSkeletonTests();

    return ailo::test::finish();
}
