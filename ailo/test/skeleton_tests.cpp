#include "render/Skeleton.h"
#include "Testing.h"

#include <glm/gtc/matrix_transform.hpp>

using namespace ailo;

namespace {

glm::mat4 translation(const glm::vec3& position) {
    return glm::translate(glm::mat4(1.0f), position);
}

// Armature (non-bone) -> Hips -> Spine, each moved along a different axis.
Skeleton makeSkeleton() {
    Skeleton skeleton;
    skeleton.nodes = { { "Armature", -1 }, { "Hips", 0 }, { "Spine", 1 } };
    skeleton.restPose = {
        NodeTransform { .position = { 1.0f, 0.0f, 0.0f } },
        NodeTransform { .position = { 0.0f, 2.0f, 0.0f } },
        NodeTransform { .position = { 0.0f, 0.0f, 3.0f } },
    };
    skeleton.pose = skeleton.restPose;
    skeleton.updateModelPose();
    return skeleton;
}

void testModelPoseFollowsHierarchy() {
    Skeleton skeleton = makeSkeleton();
    REQUIRE_EQ(skeleton.modelPose.size(), 3);
    CHECK_EQ(skeleton.modelPose[0], translation({ 1.0f, 0.0f, 0.0f }));
    CHECK_EQ(skeleton.modelPose[1], translation({ 1.0f, 2.0f, 0.0f }));
    CHECK_EQ(skeleton.modelPose[2], translation({ 1.0f, 2.0f, 3.0f }));

    skeleton.pose[1].position = { 0.0f, 5.0f, 0.0f };
    skeleton.updateModelPose();
    CHECK_EQ(skeleton.modelPose[0], translation({ 1.0f, 0.0f, 0.0f }));
    CHECK_EQ(skeleton.modelPose[2], translation({ 1.0f, 5.0f, 3.0f }));
}

void testBoneMatricesUseSkinOrder() {
    const Skeleton skeleton = makeSkeleton();

    // The inverse of each bone's rest transform, in a different order from the nodes.
    Skin skin;
    skin.nodeIndices = { 2, 1 };
    skin.inverseBinds = { glm::inverse(skeleton.modelPose[2]), glm::inverse(skeleton.modelPose[1]) };

    std::vector<glm::mat4> bones(2);
    CHECK(computeBoneMatrices(skeleton, skin, bones));
    CHECK_EQ(bones[0], glm::mat4(1.0f));
    CHECK_EQ(bones[1], glm::mat4(1.0f));

    skin.inverseBinds = { glm::mat4(1.0f), translation({ 0.0f, 0.0f, 7.0f }) };
    CHECK(computeBoneMatrices(skeleton, skin, bones));
    CHECK_EQ(bones[0], skeleton.modelPose[2]);
    CHECK_EQ(bones[1], skeleton.modelPose[1] * translation({ 0.0f, 0.0f, 7.0f }));
}

void testBoneOutsideSkeletonIsIdentity() {
    const Skeleton skeleton = makeSkeleton();

    Skin skin;
    skin.nodeIndices = { 3, 1 };
    skin.inverseBinds = { translation({ 9.0f, 0.0f, 0.0f }), glm::mat4(1.0f) };

    std::vector<glm::mat4> bones(2);
    CHECK(!computeBoneMatrices(skeleton, skin, bones));
    CHECK_EQ(bones[0], glm::mat4(1.0f));
    CHECK_EQ(bones[1], skeleton.modelPose[1]);
}

}

void runSkeletonTests() {
    ailo::test::run("skeleton/testModelPoseFollowsHierarchy", testModelPoseFollowsHierarchy);
    ailo::test::run("skeleton/testBoneMatricesUseSkinOrder", testBoneMatricesUseSkinOrder);
    ailo::test::run("skeleton/testBoneOutsideSkeletonIsIdentity", testBoneOutsideSkeletonIsIdentity);
}
