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
void runSkeletonTests();
void runExprVMTests();
void runAnimGraphTests();
void runSyncTrackTests();
void runFootMarkerTests();

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
    runSkeletonTests();
    runExprVMTests();
    runAnimGraphTests();
    runSyncTrackTests();
    runFootMarkerTests();

    return ailo::test::finish();
}
