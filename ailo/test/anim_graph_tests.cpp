#include "ecs/AnimGraph.h"
#include "render/Skeleton.h"
#include "Testing.h"

#include <string>
#include <vector>

using namespace ailo;

namespace {

using MachineBuilder = AnimGraph::MachineBuilder;

// Each clip moves a bone of its own along x by one unit per second of its time, so the evaluated pose shows where
// every clip is.
struct Fixture {
    AssetStorage<AnimationClip> clipStorage;
    AssetStorage<AnimGraph> graphStorage;
    std::vector<AssetPtr<AnimationClip>> clips;
    Skeleton skeleton;
    PosePool posePool;

    explicit Fixture(std::initializer_list<const char*> names) {
        for (const char* name : names) {
            AnimationClip clip { .name = name };
            BoneChannel& channel = clip.channels.emplace_back();
            channel.boneName = name;
            channel.rotationKeys = { { 0.0f, glm::quat(1.0f, 0.0f, 0.0f, 0.0f) } };
            channel.scaleKeys = { { 0.0f, glm::vec3(1.0f) } };
            clip.nameToIndex[name] = 0;
            clips.push_back(clipStorage.emplace(noname_t {}, std::move(clip)));
            setDuration(clips.size() - 1, 1.0f);

            skeleton.nodes.push_back({ .name = name, .parentIndex = -1 });
            skeleton.restPose.emplace_back();
        }
    }

    void setDuration(size_t clip, float duration) {
        clips[clip]->duration = duration;
        clips[clip]->channels[0].positionKeys = { { 0.0f, glm::vec3(0.0f) }, { duration, glm::vec3(duration, 0, 0) } };
    }

    AnimGraphRunner run(const AnimGraph::Builder& builder) {
        return AnimGraphRunner(graphStorage.emplace(noname_t {}, builder.build()), clips);
    }

    // The evaluated x of the clip's bone: its time in seconds, scaled by its weight while fading.
    float pose(AnimGraphRunner& runner, size_t clip) {
        std::vector<NodeTransform> out;
        runner.evaluate(skeleton, posePool, out);
        return out[clip].position.x;
    }
};

MachineBuilder twoStates(AnimGraph::MachineBuilder::TransitionDesc desc) {
    MachineBuilder machine;
    machine.state("a").state("b").transition(0, 1, std::move(desc));
    return machine;
}

void testBuildErrors() {
    CHECK_THROWS(AnimGraph::Builder {}.build());
    CHECK_THROWS(AnimGraph::Builder {}.root(twoStates({ .condition = "missing > 0" })).build());
    CHECK_THROWS(AnimGraph::Builder {}.parameter("speed").root(twoStates({ .condition = "speed >" })).build());
    CHECK_THROWS(AnimGraph::Builder {}.parameter("speed").parameter("speed").root(twoStates({})).build());
}

void testParameters() {
    MachineBuilder nested;
    nested.state("b");
    MachineBuilder machine;
    machine.state("a")
        .state(std::move(nested))
        .transition(0, 1, { .condition = "1 < 2" })
        .transition(1, 0, { .condition = "1 > 2" });
    const AnimGraph graph = AnimGraph::Builder {}
        .parameter("speed", 1.5f)
        .parameter("grounded", 1.0f)
        .root(std::move(machine))
        .build();

    REQUIRE_EQ(graph.parameterCount(), 2u);
    REQUIRE(graph.findParameter("speed").has_value());
    REQUIRE(graph.findParameter("grounded").has_value());
    CHECK(!graph.findParameter("missing").has_value());
    CHECK_EQ(graph.parameterDefault(*graph.findParameter("speed")), 1.5f);
    CHECK_EQ(graph.parameterName(*graph.findParameter("grounded")), std::string("grounded"));

    // A constant-true condition is dropped; a constant-false one is kept so the transition never fires.
    const auto& root = std::get<StateMachineNode>(graph.node(AnimGraph::kRoot));
    CHECK(!root.transitionsFrom(0)[0].condition.has_value());
    CHECK(root.transitionsFrom(1)[0].condition.has_value());
}

void testConditionGatesTransition() {
    Fixture fixture { "a", "b" };
    AnimGraphRunner runner = fixture.run(
        AnimGraph::Builder {}.parameter("speed").root(twoStates({ .condition = "speed > 1" })));

    CHECK_EQ(runner.getFloat("speed"), 0.0f);
    runner.update(0.1f);
    CHECK_EQ(runner.currentState(), 0);

    runner.setFloat("speed", 2.0f);
    CHECK_EQ(runner.getFloat("speed"), 2.0f);
    runner.update(0.1f);
    CHECK_EQ(runner.currentState(), 1);
}

void testTriggerKeptWhenConditionFails() {
    Fixture fixture { "a", "b" };
    AnimGraphRunner runner = fixture.run(
        AnimGraph::Builder {}.parameter("ready").root(twoStates({ .trigger = "go", .condition = "ready" })));

    runner.setTrigger("go");
    runner.update(0.1f);
    CHECK_EQ(runner.currentState(), 0);

    runner.setFloat("ready", 1.0f);
    runner.update(0.1f);
    CHECK_EQ(runner.currentState(), 1);
}

void testNestedMachineReadsParameters() {
    Fixture fixture { "a", "b" };
    MachineBuilder machine;
    machine.state(twoStates({ .condition = "x > 0 && y" }));
    const AssetPtr<AnimGraph> graph = fixture.graphStorage.emplace(noname_t {}, AnimGraph::Builder {}
        .parameter("x")
        .parameter("y", 1.0f)
        .root(std::move(machine))
        .build());
    AnimGraphRunner runner(graph, fixture.clips);

    const NodeIndex nested { 1 };
    runner.update(0.1f);
    CHECK_EQ(runner.currentState(nested), 0);

    const auto x = graph->findParameter("x");
    REQUIRE(x.has_value());
    runner.setFloat(*x, 3.0f);
    runner.update(0.1f);
    CHECK_EQ(runner.currentState(nested), 1);
}

constexpr float kEps = 1e-5f;

AnimGraph::Builder oneState(bool looping = true, float rate = 1.0f) {
    MachineBuilder machine;
    machine.state("a", looping, rate);
    return std::move(AnimGraph::Builder {}.root(std::move(machine)));
}

void testClipTime() {
    Fixture fixture { "a" };
    fixture.setDuration(0, 2.0f);

    AnimGraphRunner looping = fixture.run(oneState());
    looping.update(0.5f);
    CHECK_NEAR(fixture.pose(looping, 0), 0.5f, kEps);
    looping.update(2.0f);
    CHECK_NEAR(fixture.pose(looping, 0), 0.5f, kEps);

    AnimGraphRunner once = fixture.run(oneState(false));
    once.update(3.0f);
    CHECK_NEAR(fixture.pose(once, 0), 2.0f, kEps);

    AnimGraphRunner reversed = fixture.run(oneState(true, -1.0f));
    reversed.update(0.5f);
    CHECK_NEAR(fixture.pose(reversed, 0), 1.5f, kEps);
    reversed.update(1.75f);
    CHECK_NEAR(fixture.pose(reversed, 0), 1.75f, kEps);
}

}

void runAnimGraphTests() {
    ailo::test::run("anim_graph/testBuildErrors", testBuildErrors);
    ailo::test::run("anim_graph/testParameters", testParameters);
    ailo::test::run("anim_graph/testConditionGatesTransition", testConditionGatesTransition);
    ailo::test::run("anim_graph/testTriggerKeptWhenConditionFails", testTriggerKeptWhenConditionFails);
    ailo::test::run("anim_graph/testNestedMachineReadsParameters", testNestedMachineReadsParameters);
    ailo::test::run("anim_graph/testClipTime", testClipTime);
}
