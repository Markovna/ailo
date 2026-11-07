#include <stdexcept>

#include "ecs/AnimationStateMachine.h"
#include "Testing.h"

using namespace ailo;

namespace {

struct Fixture {
    AssetStorage<AnimationClip> clipStorage;
    AssetStorage<AnimationStateMachine> machines;
    std::vector<AssetPtr<AnimationClip>> clips;

    Fixture() {
        for (const char* name : { "c0", "c1", "c2", "c3" }) {
            clips.push_back(clipStorage.emplace(noname_t {}, AnimationClip { .name = name }));
        }
    }

    AssetPtr<AnimationStateMachine> add(const AnimationStateMachine& definition) {
        return machines.emplace(noname_t {}, definition);
    }

    AnimationStateMachineInstance instance(const AnimationStateMachine& definition) {
        return AnimationStateMachineInstance(add(definition), clips);
    }
};

void testStartsInFirstState() {
    Fixture fixture;
    AnimationStateMachine definition;
    definition.addState("c3");
    definition.addState("c1");

    auto sm = fixture.instance(definition);
    CHECK_EQ(sm.currentStateIndex(), 0u);
    CHECK_EQ(sm.currentClip(), 3u);
    CHECK(sm.update(0.0f) == nullptr);
}

void testEmptyDefinitionNeverTransitions() {
    Fixture fixture;
    AnimationStateMachine definition;
    auto sm = fixture.instance(definition);
    CHECK(sm.update(0.0f) == nullptr);
}

void testTriggerFiresAndIsConsumed() {
    Fixture fixture;
    AnimationStateMachine definition;
    auto idle = definition.addState("c0");
    auto run = definition.addState("c1");
    definition.addTransition(idle, run, "go", 0.25f);
    definition.addTransition(run, idle, "go");

    auto sm = fixture.instance(definition);
    sm.setTrigger("go");
    auto* taken = sm.update(0.0f);
    REQUIRE(taken != nullptr);
    CHECK_EQ(taken->to, run);
    CHECK_NEAR(taken->fadeDuration, 0.25f, 1e-6f);
    CHECK_EQ(sm.currentStateIndex(), run);

    CHECK(sm.update(0.0f) == nullptr);
    CHECK_EQ(sm.currentStateIndex(), run);
}

void testResetTriggerCancels() {
    Fixture fixture;
    AnimationStateMachine definition;
    auto a = definition.addState("c0");
    auto b = definition.addState("c1");
    definition.addTransition(a, b, "go");

    auto sm = fixture.instance(definition);
    sm.setTrigger("go");
    sm.resetTrigger("go");
    CHECK(sm.update(0.0f) == nullptr);
    CHECK_EQ(sm.currentStateIndex(), a);
}

void testTriggerStaysSetUntilAStateUsesIt() {
    Fixture fixture;
    AnimationStateMachine definition;
    auto a = definition.addState("c0");
    auto b = definition.addState("c1");
    auto c = definition.addState("c2");
    definition.addTransition(a, b, "first");
    definition.addTransition(b, c, "second");

    auto sm = fixture.instance(definition);
    sm.setTrigger("second");
    CHECK(sm.update(0.0f) == nullptr);
    CHECK_EQ(sm.currentStateIndex(), a);

    sm.setTrigger("first");
    REQUIRE(sm.update(0.0f) != nullptr);
    CHECK_EQ(sm.currentStateIndex(), b);
    REQUIRE(sm.update(0.0f) != nullptr);
    CHECK_EQ(sm.currentStateIndex(), c);
}

void testFirstAddedTransitionWins() {
    Fixture fixture;
    AnimationStateMachine definition;
    auto a = definition.addState("c0");
    auto b = definition.addState("c1");
    auto c = definition.addState("c2");
    definition.addTransition(a, b, "toB");
    definition.addTransition(a, c, "toC");

    auto sm = fixture.instance(definition);
    sm.setTrigger("toC");
    sm.setTrigger("toB");
    REQUIRE(sm.update(0.0f) != nullptr);
    CHECK_EQ(sm.currentStateIndex(), b);
}

void testMissingClipThrows() {
    Fixture fixture;
    AnimationStateMachine definition;
    definition.addState("c0");
    definition.addState("missing");
    CHECK_THROWS(fixture.instance(definition));
}

void testClipFollowsState() {
    Fixture fixture;
    AnimationStateMachine definition;
    auto a = definition.addState("c2");
    auto b = definition.addState("c0");
    definition.addTransition(a, b, "go");

    auto sm = fixture.instance(definition);
    CHECK_EQ(sm.currentClip(), 2u);
    sm.setTrigger("go");
    REQUIRE(sm.update(0.0f) != nullptr);
    CHECK_EQ(sm.currentClip(), 0u);
}

void testInstanceKeepsDefinitionAlive() {
    Fixture fixture;
    AnimationStateMachine definition;
    definition.addState("c0");

    auto asset = fixture.add(definition);
    AnimationStateMachineInstance sm(asset, fixture.clips);
    asset.reset();
    CHECK_EQ(fixture.machines.size(), size_t(1));
    CHECK_EQ(sm.definition().stateCount(), 1u);
}

void testInstancesAreIndependent() {
    Fixture fixture;
    AnimationStateMachine definition;
    auto a = definition.addState("c0");
    auto b = definition.addState("c1");
    definition.addTransition(a, b, "go");

    auto asset = fixture.add(definition);
    AnimationStateMachineInstance first(asset, fixture.clips);
    AnimationStateMachineInstance second(asset, fixture.clips);

    first.setTrigger("go");
    CHECK(second.update(0.0f) == nullptr);
    CHECK_EQ(second.currentStateIndex(), a);

    REQUIRE(first.update(0.0f) != nullptr);
    CHECK_EQ(first.currentStateIndex(), b);
    CHECK_EQ(second.currentStateIndex(), a);
}

void testTransitionsStayGroupedByFromState() {
    Fixture fixture;
    AnimationStateMachine definition;
    auto a = definition.addState("c0");
    auto b = definition.addState("c1");
    auto c = definition.addState("c2");
    definition.addTransition(c, a, "t0");
    definition.addTransition(a, b, "t1");
    definition.addTransition(b, c, "t2");
    definition.addTransition(a, c, "t3");
    definition.addTransition(c, b, "t4");

    auto fromA = definition.transitions(a);
    auto fromB = definition.transitions(b);
    auto fromC = definition.transitions(c);
    REQUIRE_EQ(fromA.size(), size_t(2));
    REQUIRE_EQ(fromB.size(), size_t(1));
    REQUIRE_EQ(fromC.size(), size_t(2));

    CHECK_EQ(fromA[0].to, b);
    CHECK_EQ(fromA[1].to, c);
    CHECK_EQ(fromB[0].to, c);
    CHECK_EQ(fromC[0].to, a);
    CHECK_EQ(fromC[1].to, b);

    CHECK(fromA.data() + fromA.size() == fromB.data());
    CHECK(fromB.data() + fromB.size() == fromC.data());
}

void testSharedTriggerNameReusesIndex() {
    Fixture fixture;
    AnimationStateMachine definition;
    auto a = definition.addState("c0");
    auto b = definition.addState("c1");
    definition.addTransition(a, b, "go");
    definition.addTransition(b, a, "go");

    CHECK_EQ(definition.triggerCount(), 1u);
    CHECK_EQ(definition.addTrigger("go"), definition.transitions(a)[0].trigger);
    CHECK_EQ(definition.transitions(a)[0].trigger, definition.transitions(b)[0].trigger);
    CHECK(definition.findTrigger("go").has_value());
    CHECK(!definition.findTrigger("missing").has_value());
}

void testTriggerWaitsForExitTime() {
    Fixture fixture;
    AnimationStateMachine definition;
    auto a = definition.addState("c0");
    auto b = definition.addState("c1");
    definition.addTransition(a, b, "go", 0.0f, 1.0f);

    auto sm = fixture.instance(definition);
    sm.setTrigger("go");
    CHECK(sm.update(0.5f) == nullptr);
    CHECK_EQ(sm.currentStateIndex(), a);
    CHECK_NEAR(sm.stateTime(), 0.5f, 1e-6f);

    REQUIRE(sm.update(0.5f) != nullptr);
    CHECK_EQ(sm.currentStateIndex(), b);
    CHECK_NEAR(sm.stateTime(), 0.0f, 1e-6f);
}

void testExitTimeWithoutTriggerTransitionsAutomatically() {
    Fixture fixture;
    AnimationStateMachine definition;
    auto a = definition.addState("c0", false);
    auto b = definition.addState("c1");
    definition.addTransition(a, b, {}, 0.1f, 2.0f);

    auto sm = fixture.instance(definition);
    CHECK(sm.update(1.0f) == nullptr);
    auto* taken = sm.update(1.5f);
    REQUIRE(taken != nullptr);
    CHECK_EQ(taken->to, b);
    CHECK_NEAR(taken->exitTime, 2.0f, 1e-6f);
}

void testExitTimeRestartsOnStateEntry() {
    Fixture fixture;
    AnimationStateMachine definition;
    auto a = definition.addState("c0");
    auto b = definition.addState("c1");
    definition.addTransition(a, b, "go");
    definition.addTransition(b, a, "go", 0.0f, 1.0f);

    auto sm = fixture.instance(definition);
    sm.update(5.0f);
    sm.setTrigger("go");
    REQUIRE(sm.update(0.0f) != nullptr);
    CHECK_EQ(sm.currentStateIndex(), b);

    sm.setTrigger("go");
    CHECK(sm.update(0.25f) == nullptr);
    CHECK_EQ(sm.currentStateIndex(), b);
    REQUIRE(sm.update(0.75f) != nullptr);
    CHECK_EQ(sm.currentStateIndex(), a);
}

void testLaterTransitionFiresWhileEarlierWaitsForExitTime() {
    Fixture fixture;
    AnimationStateMachine definition;
    auto a = definition.addState("c0");
    auto b = definition.addState("c1");
    auto c = definition.addState("c2");
    definition.addTransition(a, b, "go", 0.0f, 1.0f);
    definition.addTransition(a, c, "go");

    auto sm = fixture.instance(definition);
    sm.setTrigger("go");
    REQUIRE(sm.update(0.1f) != nullptr);
    CHECK_EQ(sm.currentStateIndex(), c);
}

}

void runAnimationStateMachineTests() {
    ailo::test::run("animsm/testStartsInFirstState", testStartsInFirstState);
    ailo::test::run("animsm/testEmptyDefinitionNeverTransitions", testEmptyDefinitionNeverTransitions);
    ailo::test::run("animsm/testTriggerFiresAndIsConsumed", testTriggerFiresAndIsConsumed);
    ailo::test::run("animsm/testResetTriggerCancels", testResetTriggerCancels);
    ailo::test::run("animsm/testTriggerStaysSetUntilAStateUsesIt", testTriggerStaysSetUntilAStateUsesIt);
    ailo::test::run("animsm/testFirstAddedTransitionWins", testFirstAddedTransitionWins);
    ailo::test::run("animsm/testMissingClipThrows", testMissingClipThrows);
    ailo::test::run("animsm/testClipFollowsState", testClipFollowsState);
    ailo::test::run("animsm/testInstanceKeepsDefinitionAlive", testInstanceKeepsDefinitionAlive);
    ailo::test::run("animsm/testInstancesAreIndependent", testInstancesAreIndependent);
    ailo::test::run("animsm/testTransitionsStayGroupedByFromState", testTransitionsStayGroupedByFromState);
    ailo::test::run("animsm/testSharedTriggerNameReusesIndex", testSharedTriggerNameReusesIndex);
    ailo::test::run("animsm/testTriggerWaitsForExitTime", testTriggerWaitsForExitTime);
    ailo::test::run("animsm/testExitTimeWithoutTriggerTransitionsAutomatically", testExitTimeWithoutTriggerTransitionsAutomatically);
    ailo::test::run("animsm/testExitTimeRestartsOnStateEntry", testExitTimeRestartsOnStateEntry);
    ailo::test::run("animsm/testLaterTransitionFiresWhileEarlierWaitsForExitTime", testLaterTransitionFiresWhileEarlierWaitsForExitTime);
}
