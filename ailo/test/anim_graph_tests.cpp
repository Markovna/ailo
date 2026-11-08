#include <stdexcept>

#include "ecs/AnimGraph.h"
#include "render/Skeleton.h"
#include "Testing.h"

using namespace ailo;

namespace {

struct Fixture {
    AssetStorage<AnimationClip> clipStorage;
    AssetStorage<AnimGraph> graphs;
    std::vector<AssetPtr<AnimationClip>> clips;

    Fixture() {
        for (const char* name : { "c0", "c1", "c2", "c3" }) {
            clips.push_back(clipStorage.emplace(noname_t {}, AnimationClip { .name = name, .duration = 2.0f }));
        }
    }

    AnimGraphInstance instance(const AnimGraph& definition) {
        return AnimGraphInstance(graphs.emplace(noname_t {}, definition), clips);
    }
};

const ClipPlayback& playback(const AnimationBlend& blend, AnimBlendId id) {
    return blend.find(id)->clip;
}

// Entries directly in the blend's root group.
size_t rootChildren(const AnimationBlend& blend) {
    const auto entries = blend.entries();
    size_t count = 0;
    for (uint32_t child = 1; child < entries[0].subtreeSize; child += entries[child].subtreeSize) {
        count++;
    }
    return count;
}

void testStartsInEntryState() {
    Fixture fixture;
    AnimGraph definition;
    definition.addState("c3");
    definition.addState("c1");

    auto graph = fixture.instance(definition);
    CHECK_EQ(graph.currentStateIndex(), 0u);
    CHECK_EQ(graph.currentClip(), 3u);
    CHECK_EQ(graph.currentEntry(), kBlendNone);

    AnimationBlend blend;
    CHECK(graph.update(0.0f, blend) == nullptr);
    REQUIRE_EQ(blend.entries().size(), size_t(2));
    CHECK_EQ(blend.entries()[1].id, graph.currentEntry());
    CHECK_EQ(playback(blend, graph.currentEntry()).clip, 3u);
    CHECK_NEAR(blend.entries()[1].fade(), 1.0f, 1e-6f);
}

void testEmptyGraphPlaysNothing() {
    Fixture fixture;
    AnimGraph definition;
    auto graph = fixture.instance(definition);
    AnimationBlend blend;
    CHECK(graph.update(0.1f, blend) == nullptr);
    CHECK_EQ(blend.entries().size(), size_t(1));
}

void testMissingClipThrows() {
    Fixture fixture;
    AnimGraph definition;
    definition.addState("c0");
    definition.addState("missing");
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
    AnimationBlend blend;
    graph.setTrigger("go");
    auto* taken = graph.update(0.0f, blend);
    REQUIRE(taken != nullptr);
    CHECK_EQ(taken->to, run);
    CHECK_EQ(graph.currentStateIndex(), run);
    CHECK_EQ(graph.currentClip(), 1u);
    CHECK_EQ(rootChildren(blend), size_t(2));
    CHECK_EQ(playback(blend, graph.currentEntry()).clip, 1u);
    CHECK_NEAR(blend.find(graph.currentEntry())->fadeDuration, 0.25f, 1e-6f);

    CHECK(graph.update(0.0f, blend) == nullptr);
    CHECK_EQ(graph.currentStateIndex(), run);
}

void testResetTriggerCancels() {
    Fixture fixture;
    AnimGraph definition;
    auto a = definition.addState("c0");
    auto b = definition.addState("c1");
    definition.addTransition(a, b, "go");

    auto graph = fixture.instance(definition);
    AnimationBlend blend;
    graph.setTrigger("go");
    graph.resetTrigger("go");
    CHECK(graph.update(0.0f, blend) == nullptr);
    CHECK_EQ(graph.currentStateIndex(), a);
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
    AnimationBlend blend;
    graph.setTrigger("toC");
    graph.setTrigger("toB");
    REQUIRE(graph.update(0.0f, blend) != nullptr);
    CHECK_EQ(graph.currentStateIndex(), b);
}

void testExitTimeRestartsOnStateEntry() {
    Fixture fixture;
    AnimGraph definition;
    auto a = definition.addState("c0");
    auto b = definition.addState("c1");
    definition.addTransition(a, b, "go");
    definition.addTransition(b, a, "go", 0.0f, 1.0f);

    auto graph = fixture.instance(definition);
    AnimationBlend blend;
    graph.update(5.0f, blend);
    graph.setTrigger("go");
    REQUIRE(graph.update(0.0f, blend) != nullptr);
    CHECK_NEAR(graph.stateTime(), 0.0f, 1e-6f);

    graph.setTrigger("go");
    CHECK(graph.update(0.25f, blend) == nullptr);
    CHECK_NEAR(graph.stateTime(), 0.25f, 1e-6f);
    REQUIRE(graph.update(0.75f, blend) != nullptr);
    CHECK_EQ(graph.currentStateIndex(), a);
}

void testEnteredStateAdvancesThisUpdate() {
    Fixture fixture;
    AnimGraph definition;
    auto a = definition.addState("c0");
    auto b = definition.addState("c1");
    definition.addTransition(a, b, "go", 1.0f);

    auto graph = fixture.instance(definition);
    AnimationBlend blend;
    graph.setTrigger("go");
    graph.update(0.2f, blend);
    blend.step(0.2f);

    const auto* entered = blend.find(graph.currentEntry());
    REQUIRE(entered != nullptr);
    CHECK_NEAR(entered->localWeight, 0.2f, 1e-6f);
    CHECK_NEAR(entered->clip.time, 0.2f, 1e-6f);
}

void testReentryPlaysAfresh() {
    Fixture fixture;
    AnimGraph definition;
    auto a = definition.addState("c0");
    definition.addTransition(a, a, "go", 1.0f);

    auto graph = fixture.instance(definition);
    AnimationBlend blend;
    graph.update(0.5f, blend);
    blend.step(0.5f);
    const auto first = graph.currentEntry();
    graph.setTrigger("go");
    graph.update(0.1f, blend);
    blend.step(0.1f);

    CHECK(graph.currentEntry() != first);
    REQUIRE_EQ(rootChildren(blend), size_t(2));
    CHECK_NEAR(playback(blend, first).time, 0.6f, 1e-5f);
    CHECK_NEAR(playback(blend, graph.currentEntry()).time, 0.1f, 1e-5f);
}

void testClipSettingsReachPlayback() {
    Fixture fixture;
    AnimGraph definition;
    definition.addState("c2", false);

    auto graph = fixture.instance(definition);
    AnimationBlend blend;
    graph.update(0.0f, blend);
    const auto& clip = playback(blend, graph.currentEntry());
    CHECK_EQ(clip.clip, 2u);
    CHECK(!clip.looping);
    CHECK_NEAR(clip.duration, 2.0f, 1e-6f);
    CHECK_NEAR(clip.speed, 1.0f, 1e-6f);
}

void testGroupArgumentPicksBlendGroup() {
    Fixture fixture;
    AnimGraph definition;
    definition.addState("c0");

    auto graph = fixture.instance(definition);
    AnimationBlend blend;
    const auto group = blend.playGroup(blend.root(), 0.0f);
    graph.update(0.0f, blend, group);
    const auto* entry = blend.find(graph.currentEntry());
    REQUIRE(entry != nullptr);
    CHECK_EQ(entry->parent, blend.indexOf(group));
}

}

void runAnimGraphTests() {
    ailo::test::run("animgraph/testStartsInEntryState", testStartsInEntryState);
    ailo::test::run("animgraph/testEmptyGraphPlaysNothing", testEmptyGraphPlaysNothing);
    ailo::test::run("animgraph/testMissingClipThrows", testMissingClipThrows);
    ailo::test::run("animgraph/testTriggerFiresAndIsConsumed", testTriggerFiresAndIsConsumed);
    ailo::test::run("animgraph/testResetTriggerCancels", testResetTriggerCancels);
    ailo::test::run("animgraph/testFirstAddedTransitionWins", testFirstAddedTransitionWins);
    ailo::test::run("animgraph/testExitTimeRestartsOnStateEntry", testExitTimeRestartsOnStateEntry);
    ailo::test::run("animgraph/testEnteredStateAdvancesThisUpdate", testEnteredStateAdvancesThisUpdate);
    ailo::test::run("animgraph/testReentryPlaysAfresh", testReentryPlaysAfresh);
    ailo::test::run("animgraph/testClipSettingsReachPlayback", testClipSettingsReachPlayback);
    ailo::test::run("animgraph/testGroupArgumentPicksBlendGroup", testGroupArgumentPicksBlendGroup);
}
