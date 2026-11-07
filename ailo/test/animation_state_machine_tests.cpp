#include "ecs/AnimationStateMachine.h"
#include "Testing.h"

using namespace ailo;

namespace {

void testStartsInFirstState() {
    AnimationStateMachine definition;
    definition.addState(5);
    definition.addState(6);

    AnimationStateMachineInstance sm(definition);
    CHECK_EQ(sm.currentStateIndex(), 0u);
    CHECK_EQ(sm.currentState().clip, 5u);
    CHECK(sm.update(0.0f) == nullptr);
}

void testEmptyDefinitionNeverTransitions() {
    AnimationStateMachine definition;
    AnimationStateMachineInstance sm(definition);
    CHECK(sm.update(0.0f) == nullptr);
}

void testTriggerFiresAndIsConsumed() {
    AnimationStateMachine definition;
    auto idle = definition.addState(0);
    auto run = definition.addState(1);
    definition.addTransition(idle, run, "go", 0.25f);
    definition.addTransition(run, idle, "go");

    AnimationStateMachineInstance sm(definition);
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
    AnimationStateMachine definition;
    auto a = definition.addState(0);
    auto b = definition.addState(1);
    definition.addTransition(a, b, "go");

    AnimationStateMachineInstance sm(definition);
    sm.setTrigger("go");
    sm.resetTrigger("go");
    CHECK(sm.update(0.0f) == nullptr);
    CHECK_EQ(sm.currentStateIndex(), a);
}

void testTriggerStaysSetUntilAStateUsesIt() {
    AnimationStateMachine definition;
    auto a = definition.addState(0);
    auto b = definition.addState(1);
    auto c = definition.addState(2);
    definition.addTransition(a, b, "first");
    definition.addTransition(b, c, "second");

    AnimationStateMachineInstance sm(definition);
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
    AnimationStateMachine definition;
    auto a = definition.addState(0);
    auto b = definition.addState(1);
    auto c = definition.addState(2);
    definition.addTransition(a, b, "toB");
    definition.addTransition(a, c, "toC");

    AnimationStateMachineInstance sm(definition);
    sm.setTrigger("toC");
    sm.setTrigger("toB");
    REQUIRE(sm.update(0.0f) != nullptr);
    CHECK_EQ(sm.currentStateIndex(), b);
}

void testInstancesAreIndependent() {
    AnimationStateMachine definition;
    auto a = definition.addState(0);
    auto b = definition.addState(1);
    definition.addTransition(a, b, "go");

    AnimationStateMachineInstance first(definition);
    AnimationStateMachineInstance second(definition);

    first.setTrigger("go");
    CHECK(second.update(0.0f) == nullptr);
    CHECK_EQ(second.currentStateIndex(), a);

    REQUIRE(first.update(0.0f) != nullptr);
    CHECK_EQ(first.currentStateIndex(), b);
    CHECK_EQ(second.currentStateIndex(), a);
}

void testTransitionsStayGroupedByFromState() {
    AnimationStateMachine definition;
    auto a = definition.addState(0);
    auto b = definition.addState(1);
    auto c = definition.addState(2);
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
    AnimationStateMachine definition;
    auto a = definition.addState(0);
    auto b = definition.addState(1);
    definition.addTransition(a, b, "go");
    definition.addTransition(b, a, "go");

    CHECK_EQ(definition.triggerCount(), 1u);
    CHECK_EQ(definition.addTrigger("go"), definition.transitions(a)[0].trigger);
    CHECK_EQ(definition.transitions(a)[0].trigger, definition.transitions(b)[0].trigger);
    CHECK(definition.findTrigger("go").has_value());
    CHECK(!definition.findTrigger("missing").has_value());
}

void testTriggerWaitsForExitTime() {
    AnimationStateMachine definition;
    auto a = definition.addState(0);
    auto b = definition.addState(1);
    definition.addTransition(a, b, "go", 0.0f, 1.0f);

    AnimationStateMachineInstance sm(definition);
    sm.setTrigger("go");
    CHECK(sm.update(0.5f) == nullptr);
    CHECK_EQ(sm.currentStateIndex(), a);
    CHECK_NEAR(sm.stateTime(), 0.5f, 1e-6f);

    REQUIRE(sm.update(0.5f) != nullptr);
    CHECK_EQ(sm.currentStateIndex(), b);
    CHECK_NEAR(sm.stateTime(), 0.0f, 1e-6f);
}

void testExitTimeWithoutTriggerTransitionsAutomatically() {
    AnimationStateMachine definition;
    auto a = definition.addState(0, false);
    auto b = definition.addState(1);
    definition.addTransition(a, b, {}, 0.1f, 2.0f);

    AnimationStateMachineInstance sm(definition);
    CHECK(sm.update(1.0f) == nullptr);
    auto* taken = sm.update(1.5f);
    REQUIRE(taken != nullptr);
    CHECK_EQ(taken->to, b);
    CHECK_NEAR(taken->exitTime, 2.0f, 1e-6f);
}

void testExitTimeRestartsOnStateEntry() {
    AnimationStateMachine definition;
    auto a = definition.addState(0);
    auto b = definition.addState(1);
    definition.addTransition(a, b, "go");
    definition.addTransition(b, a, "go", 0.0f, 1.0f);

    AnimationStateMachineInstance sm(definition);
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
    AnimationStateMachine definition;
    auto a = definition.addState(0);
    auto b = definition.addState(1);
    auto c = definition.addState(2);
    definition.addTransition(a, b, "go", 0.0f, 1.0f);
    definition.addTransition(a, c, "go");

    AnimationStateMachineInstance sm(definition);
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
    ailo::test::run("animsm/testInstancesAreIndependent", testInstancesAreIndependent);
    ailo::test::run("animsm/testTransitionsStayGroupedByFromState", testTransitionsStayGroupedByFromState);
    ailo::test::run("animsm/testSharedTriggerNameReusesIndex", testSharedTriggerNameReusesIndex);
    ailo::test::run("animsm/testTriggerWaitsForExitTime", testTriggerWaitsForExitTime);
    ailo::test::run("animsm/testExitTimeWithoutTriggerTransitionsAutomatically", testExitTimeWithoutTriggerTransitionsAutomatically);
    ailo::test::run("animsm/testExitTimeRestartsOnStateEntry", testExitTimeRestartsOnStateEntry);
    ailo::test::run("animsm/testLaterTransitionFiresWhileEarlierWaitsForExitTime", testLaterTransitionFiresWhileEarlierWaitsForExitTime);
}
