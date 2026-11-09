#include <stdexcept>

#include "ecs/AnimGraph.h"
#include "render/Skeleton.h"
#include "Testing.h"

using namespace ailo;

namespace {

AnimationClip makeClip(const char* name, float x) {
    AnimationClip clip { .name = name, .duration = 2.0f };
    BoneChannel channel { .boneName = "b" };
    channel.positionKeys.push_back({ 0.0f, glm::vec3(x, 0.0f, 0.0f) });
    channel.rotationKeys.push_back({ 0.0f, glm::quat(1.0f, 0.0f, 0.0f, 0.0f) });
    channel.scaleKeys.push_back({ 0.0f, glm::vec3(1.0f) });
    clip.channels.push_back(std::move(channel));
    clip.nameToIndex["b"] = 0;
    return clip;
}

struct Fixture {
    AssetStorage<AnimationClip> clipStorage;
    AssetStorage<AnimGraph> graphs;
    std::vector<AssetPtr<AnimationClip>> clips;
    Skeleton skeleton;

    Fixture() {
        const char* names[] = { "c0", "c1", "c2", "c3" };
        for (int i = 0; i < 4; ++i) {
            clips.push_back(clipStorage.emplace(noname_t {}, makeClip(names[i], static_cast<float>(i))));
        }
        skeleton.nodes.push_back({ .name = "b", .parentIndex = -1 });
        skeleton.restPose.push_back({});
    }

    AnimGraphInstance instance(const AnimGraph& definition) {
        return AnimGraphInstance(graphs.emplace(noname_t {}, definition), clips);
    }

    float poseX(AnimGraphInstance& graph) {
        std::vector<NodeTransform> pose;
        REQUIRE(graph.evaluate(skeleton, clips, pose));
        REQUIRE_EQ(pose.size(), size_t(1));
        return pose[0].position.x;
    }
};

uint32_t nodeOf(const AnimGraph& graph, uint32_t machine, uint32_t state) {
    return graph.machine(machine).states[state].node;
}

void testStartsInEntryState() {
    Fixture fixture;
    AnimGraph definition;
    definition.addState("c3");
    auto b = definition.addState("c1");
    definition.setEntryState(0, b);

    auto graph = fixture.instance(definition);
    CHECK_EQ(graph.currentState(), b);
    graph.update(0.0f);
    CHECK_NEAR(graph.weight(nodeOf(definition, 0, b)), 1.0f, 1e-6f);
    CHECK_NEAR(graph.weight(nodeOf(definition, 0, 0)), 0.0f, 1e-6f);
    CHECK_NEAR(fixture.poseX(graph), 1.0f, 1e-6f);
}

void testEmptyGraphEvaluatesNothing() {
    Fixture fixture;
    AnimGraph definition;
    auto graph = fixture.instance(definition);
    graph.update(0.1f);
    std::vector<NodeTransform> pose;
    CHECK(!graph.evaluate(fixture.skeleton, fixture.clips, pose));
    CHECK(pose.empty());
}

void testMissingClipThrows() {
    Fixture fixture;
    AnimGraph definition;
    definition.addState("c0");
    definition.addState("missing");
    CHECK_THROWS(fixture.instance(definition));
}

void testSharedNodeThrows() {
    Fixture fixture;
    AnimGraph definition;
    auto sub = definition.addMachine();
    definition.addState(sub, "c0");
    definition.addMachineState(0, sub);
    definition.addMachineState(0, sub);
    CHECK_THROWS(fixture.instance(definition));
}

void testCycleThrows() {
    Fixture fixture;
    AnimGraph definition;
    auto sub = definition.addMachine();
    definition.addMachineState(0, sub);
    definition.addMachineState(sub, 0);
    CHECK_THROWS(fixture.instance(definition));
}

void testTriggerFiresAndIsConsumed() {
    Fixture fixture;
    AnimGraph definition;
    auto idle = definition.addState("c0");
    auto run = definition.addState("c1");
    definition.addTransition(idle, run, "go", 0.25f);
    definition.addTransition(run, idle, "go");

    auto graph = fixture.instance(definition);
    graph.setTrigger("go");
    graph.update(0.1f);
    CHECK_EQ(graph.currentState(), run);
    CHECK_NEAR(graph.weight(nodeOf(definition, 0, run)), 0.4f, 1e-5f);
    CHECK_NEAR(graph.weight(nodeOf(definition, 0, idle)), 0.6f, 1e-5f);

    graph.update(0.0f);
    CHECK_EQ(graph.currentState(), run);
}

void testResetTriggerCancels() {
    Fixture fixture;
    AnimGraph definition;
    auto a = definition.addState("c0");
    auto b = definition.addState("c1");
    definition.addTransition(a, b, "go");

    auto graph = fixture.instance(definition);
    graph.setTrigger("go");
    graph.resetTrigger("go");
    graph.update(0.0f);
    CHECK_EQ(graph.currentState(), a);
}

void testFirstAddedTransitionWins() {
    Fixture fixture;
    AnimGraph definition;
    auto a = definition.addState("c0");
    auto b = definition.addState("c1");
    auto c = definition.addState("c2");
    definition.addTransition(a, b, "toB");
    definition.addTransition(a, c, "toC");

    auto graph = fixture.instance(definition);
    graph.setTrigger("toC");
    graph.setTrigger("toB");
    graph.update(0.0f);
    CHECK_EQ(graph.currentState(), b);
}

void testExitTimeRestartsOnStateEntry() {
    Fixture fixture;
    AnimGraph definition;
    auto a = definition.addState("c0");
    auto b = definition.addState("c1");
    definition.addTransition(a, b, "go");
    definition.addTransition(b, a, "go", 0.0f, 1.0f);

    auto graph = fixture.instance(definition);
    graph.update(5.0f);
    graph.setTrigger("go");
    graph.update(0.0f);
    CHECK_EQ(graph.currentState(), b);
    CHECK_NEAR(graph.stateTime(), 0.0f, 1e-6f);

    graph.setTrigger("go");
    graph.update(0.25f);
    CHECK_EQ(graph.currentState(), b);
    CHECK_NEAR(graph.stateTime(), 0.25f, 1e-6f);
    graph.update(0.75f);
    CHECK_EQ(graph.currentState(), a);
}

void testEnteredStateAdvancesThisUpdate() {
    Fixture fixture;
    AnimGraph definition;
    auto a = definition.addState("c0");
    auto b = definition.addState("c1");
    definition.addTransition(a, b, "go", 1.0f);

    auto graph = fixture.instance(definition);
    graph.setTrigger("go");
    graph.update(0.2f);
    CHECK_NEAR(graph.weight(nodeOf(definition, 0, b)), 0.2f, 1e-6f);
    CHECK_NEAR(graph.clipTime(nodeOf(definition, 0, b)), 0.2f, 1e-6f);
}

void testOlderFadeOutKeepsItsTimer() {
    Fixture fixture;
    AnimGraph definition;
    auto a = definition.addState("c0");
    auto b = definition.addState("c1");
    auto c = definition.addState("c2");
    definition.addTransition(a, b, "toB", 1.0f);
    definition.addTransition(b, c, "toC", 1.0f);

    auto graph = fixture.instance(definition);
    graph.setTrigger("toB");
    graph.update(0.5f);
    graph.setTrigger("toC");
    graph.update(0.5f);

    // a finishes its own fade out; b's fade in keeps running while it fades out.
    CHECK_NEAR(graph.weight(nodeOf(definition, 0, a)), 0.0f, 1e-6f);
    CHECK_NEAR(graph.weight(nodeOf(definition, 0, b)), 0.5f, 1e-5f);
    CHECK_NEAR(graph.weight(nodeOf(definition, 0, c)), 0.5f, 1e-5f);

    graph.update(0.5f);
    CHECK_NEAR(graph.weight(nodeOf(definition, 0, b)), 0.0f, 1e-6f);
    CHECK_NEAR(graph.weight(nodeOf(definition, 0, c)), 1.0f, 1e-6f);
}

void testCutEndsAllFades() {
    Fixture fixture;
    AnimGraph definition;
    auto a = definition.addState("c0");
    auto b = definition.addState("c1");
    auto c = definition.addState("c2");
    definition.addTransition(a, b, "toB", 1.0f);
    definition.addTransition(b, c, "toC");

    auto graph = fixture.instance(definition);
    graph.setTrigger("toB");
    graph.update(0.5f);
    graph.setTrigger("toC");
    graph.update(0.1f);
    CHECK_NEAR(graph.weight(nodeOf(definition, 0, a)), 0.0f, 1e-6f);
    CHECK_NEAR(graph.weight(nodeOf(definition, 0, b)), 0.0f, 1e-6f);
    CHECK_NEAR(graph.weight(nodeOf(definition, 0, c)), 1.0f, 1e-6f);
    CHECK_NEAR(fixture.poseX(graph), 2.0f, 1e-6f);
}

void testCrossfadeRampsOverTimeLeft() {
    AnimCrossfade fade;
    fade.startFadeIn(1.0f);
    fade.step(0.5f);
    CHECK_NEAR(fade.blend(), 0.5f, 1e-6f);
    fade.fadeOutWithin(2.0f);
    fade.step(1.0f);
    CHECK_NEAR(fade.fadeIn, 1.0f, 1e-6f);
    CHECK_NEAR(fade.fadeOut, 0.5f, 1e-6f);
    fade.step(1.0f);
    CHECK_NEAR(fade.blend(), 0.0f, 1e-6f);

    fade = {};
    fade.fadeIn = 1.0f;
    fade.fadeOutWithin(1.0f);
    fade.step(0.5f);
    fade.fadeOutWithin(2.0f);
    fade.step(0.25f);
    CHECK_NEAR(fade.blend(), 0.25f, 1e-6f);
    fade.startFadeIn(1.0f);
    CHECK_NEAR(fade.fadeIn, 0.25f, 1e-6f);
    CHECK_NEAR(fade.fadeOut, 0.0f, 1e-6f);
}

void testReentryResumesFadingState() {
    Fixture fixture;
    AnimGraph definition;
    auto a = definition.addState("c0");
    auto b = definition.addState("c1");
    definition.addTransition(a, b, "go", 1.0f);
    definition.addTransition(b, a, "go", 1.0f);

    auto graph = fixture.instance(definition);
    graph.setTrigger("go");
    graph.update(0.5f);
    graph.setTrigger("go");
    graph.update(0.25f);

    CHECK_EQ(graph.currentState(), a);
    // a resumes from 0.5; b's fade in still runs under its fade out, so the blends overlap.
    CHECK_NEAR(graph.weight(nodeOf(definition, 0, a)), 0.625f, 1e-5f);
    CHECK_NEAR(graph.weight(nodeOf(definition, 0, b)), 0.5f, 1e-5f);
    CHECK_NEAR(graph.clipTime(nodeOf(definition, 0, a)), 0.75f, 1e-5f);
    CHECK_NEAR(fixture.poseX(graph), 0.5f / 1.125f, 1e-5f);
}

void testSelfTransitionOnlyRestartsStateTime() {
    Fixture fixture;
    AnimGraph definition;
    auto a = definition.addState("c0");
    definition.addTransition(a, a, "go", 1.0f);

    auto graph = fixture.instance(definition);
    graph.update(0.5f);
    graph.setTrigger("go");
    graph.update(0.25f);
    CHECK_NEAR(graph.stateTime(), 0.0f, 1e-6f);
    CHECK_NEAR(graph.clipTime(nodeOf(definition, 0, a)), 0.75f, 1e-5f);
    CHECK_NEAR(graph.weight(nodeOf(definition, 0, a)), 1.0f, 1e-6f);
}

void testClipLoopingAndClamping() {
    Fixture fixture;
    AnimGraph definition;
    auto once = definition.addState("c2", false);
    auto loop = definition.addState("c1");

    auto graph = fixture.instance(definition);
    graph.update(5.0f);
    CHECK_NEAR(graph.clipTime(nodeOf(definition, 0, once)), 2.0f, 1e-6f);

    definition.setEntryState(0, loop);
    auto looping = fixture.instance(definition);
    looping.update(2.5f);
    CHECK_NEAR(looping.clipTime(nodeOf(definition, 0, loop)), 0.5f, 1e-5f);
}

struct Nested {
    AnimGraph definition;
    uint32_t idle = 0;
    uint32_t sub = 0;      // node
    uint32_t subState = 0; // root state playing sub
    uint32_t inner0 = 0;
    uint32_t inner1 = 0;

    explicit Nested(float enterFade = 0.0f, float leaveFade = 0.0f, float innerFade = 0.0f) {
        idle = definition.addState("c0");
        sub = definition.addMachine();
        inner0 = definition.addState(sub, "c1");
        inner1 = definition.addState(sub, "c2");
        definition.addTransition(sub, inner0, inner1, "go", innerFade);
        subState = definition.addMachineState(0, sub);
        definition.addTransition(idle, subState, "enter", enterFade);
        definition.addTransition(subState, idle, "leave", leaveFade);
    }
};

void testInnerMachineWaitsUntilEntered() {
    Fixture fixture;
    Nested nested;
    auto graph = fixture.instance(nested.definition);

    graph.setTrigger("go");
    graph.update(0.1f);
    CHECK_EQ(graph.currentState(), nested.idle);
    CHECK_EQ(graph.currentState(nested.sub), nested.inner0);

    graph.setTrigger("enter");
    graph.update(0.1f);
    CHECK_EQ(graph.currentState(), nested.subState);
    CHECK_EQ(graph.currentState(nested.sub), nested.inner1);
    CHECK_NEAR(fixture.poseX(graph), 2.0f, 1e-6f);
}

void testReenteringMachineResetsIt() {
    Fixture fixture;
    Nested nested;
    auto graph = fixture.instance(nested.definition);

    graph.setTrigger("enter");
    graph.update(0.1f);
    graph.setTrigger("go");
    graph.update(0.1f);
    CHECK_EQ(graph.currentState(nested.sub), nested.inner1);

    graph.setTrigger("leave");
    graph.update(0.1f);
    graph.setTrigger("enter");
    graph.update(0.1f);
    CHECK_EQ(graph.currentState(nested.sub), nested.inner0);
    CHECK_NEAR(graph.clipTime(nodeOf(nested.definition, nested.sub, nested.inner0)), 0.1f, 1e-5f);
}

void testFadingOutMachineKeepsTriggers() {
    Fixture fixture;
    Nested nested(0.0f, 1.0f);
    auto graph = fixture.instance(nested.definition);

    graph.setTrigger("enter");
    graph.update(0.1f);
    graph.setTrigger("leave");
    graph.update(0.1f);
    CHECK_NEAR(graph.weight(nested.sub), 0.9f, 1e-5f);

    graph.setTrigger("go");
    graph.update(0.1f);
    CHECK_EQ(graph.currentState(nested.sub), nested.inner0);
    CHECK_NEAR(graph.stateTime(nested.sub), 0.3f, 1e-5f);
}

void testNestedWeightsMultiply() {
    Fixture fixture;
    Nested nested(1.0f, 0.0f, 1.0f);
    auto graph = fixture.instance(nested.definition);

    graph.setTrigger("enter");
    graph.update(0.5f);
    CHECK_NEAR(graph.weight(nested.sub), 0.5f, 1e-5f);
    CHECK_NEAR(graph.weight(nodeOf(nested.definition, nested.sub, nested.inner0)), 0.5f, 1e-5f);

    graph.setTrigger("go");
    graph.update(0.25f);
    CHECK_NEAR(graph.weight(nodeOf(nested.definition, 0, nested.idle)), 0.25f, 1e-5f);
    CHECK_NEAR(graph.weight(nested.sub), 0.75f, 1e-5f);
    CHECK_NEAR(graph.weight(nodeOf(nested.definition, nested.sub, nested.inner0)), 0.5625f, 1e-5f);
    CHECK_NEAR(graph.weight(nodeOf(nested.definition, nested.sub, nested.inner1)), 0.1875f, 1e-5f);

    // idle x=0, inner0 x=1, inner1 x=2
    CHECK_NEAR(fixture.poseX(graph), 0.5625f + 2.0f * 0.1875f, 1e-5f);
}

void testEvaluateBlendsHalfway() {
    Fixture fixture;
    AnimGraph definition;
    auto a = definition.addState("c1");
    auto b = definition.addState("c3");
    definition.addTransition(a, b, "go", 1.0f);

    auto graph = fixture.instance(definition);
    graph.update(0.0f);
    CHECK_NEAR(fixture.poseX(graph), 1.0f, 1e-6f);
    graph.setTrigger("go");
    graph.update(0.5f);
    CHECK_NEAR(fixture.poseX(graph), 2.0f, 1e-5f);
}

}

void runAnimGraphTests() {
    ailo::test::run("animgraph/testStartsInEntryState", testStartsInEntryState);
    ailo::test::run("animgraph/testEmptyGraphEvaluatesNothing", testEmptyGraphEvaluatesNothing);
    ailo::test::run("animgraph/testMissingClipThrows", testMissingClipThrows);
    ailo::test::run("animgraph/testSharedNodeThrows", testSharedNodeThrows);
    ailo::test::run("animgraph/testCycleThrows", testCycleThrows);
    ailo::test::run("animgraph/testTriggerFiresAndIsConsumed", testTriggerFiresAndIsConsumed);
    ailo::test::run("animgraph/testResetTriggerCancels", testResetTriggerCancels);
    ailo::test::run("animgraph/testFirstAddedTransitionWins", testFirstAddedTransitionWins);
    ailo::test::run("animgraph/testExitTimeRestartsOnStateEntry", testExitTimeRestartsOnStateEntry);
    ailo::test::run("animgraph/testEnteredStateAdvancesThisUpdate", testEnteredStateAdvancesThisUpdate);
    ailo::test::run("animgraph/testOlderFadeOutKeepsItsTimer", testOlderFadeOutKeepsItsTimer);
    ailo::test::run("animgraph/testCutEndsAllFades", testCutEndsAllFades);
    ailo::test::run("animgraph/testCrossfadeRampsOverTimeLeft", testCrossfadeRampsOverTimeLeft);
    ailo::test::run("animgraph/testReentryResumesFadingState", testReentryResumesFadingState);
    ailo::test::run("animgraph/testSelfTransitionOnlyRestartsStateTime", testSelfTransitionOnlyRestartsStateTime);
    ailo::test::run("animgraph/testClipLoopingAndClamping", testClipLoopingAndClamping);
    ailo::test::run("animgraph/testInnerMachineWaitsUntilEntered", testInnerMachineWaitsUntilEntered);
    ailo::test::run("animgraph/testReenteringMachineResetsIt", testReenteringMachineResetsIt);
    ailo::test::run("animgraph/testFadingOutMachineKeepsTriggers", testFadingOutMachineKeepsTriggers);
    ailo::test::run("animgraph/testNestedWeightsMultiply", testNestedWeightsMultiply);
    ailo::test::run("animgraph/testEvaluateBlendsHalfway", testEvaluateBlendsHalfway);
}
