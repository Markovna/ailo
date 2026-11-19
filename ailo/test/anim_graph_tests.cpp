#include "ecs/AnimGraph.h"
#include "render/Skeleton.h"
#include "Testing.h"

#include <cmath>
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

    const auto blendSpace = [](AnimGraph::BlendSpace1DBuilder space) {
        MachineBuilder machine;
        machine.state(std::move(space));
        return AnimGraph::Builder {}.parameter("speed").root(std::move(machine)).build();
    };
    CHECK_THROWS(blendSpace(AnimGraph::BlendSpace1DBuilder("speed")));
    CHECK_THROWS(blendSpace(std::move(AnimGraph::BlendSpace1DBuilder("missing").sample(0.0f, "a"))));
    CHECK_THROWS(blendSpace(std::move(AnimGraph::BlendSpace1DBuilder("speed").sample(1.0f, "a").sample(1.0f, "b"))));
    CHECK_THROWS(blendSpace(std::move(AnimGraph::BlendSpace1DBuilder("speed").sample(NAN, "a"))));
    CHECK_THROWS(blendSpace(std::move(AnimGraph::BlendSpace1DBuilder("speed").sample(INFINITY, "a"))));
    CHECK_THROWS(blendSpace(std::move(AnimGraph::BlendSpace1DBuilder("speed").sample(0.0f, MachineBuilder {}))));
    CHECK_THROWS(blendSpace(
        std::move(AnimGraph::BlendSpace1DBuilder("speed").sample(0.0f, twoStates({ .condition = "missing" })))));
    CHECK_THROWS(blendSpace(std::move(AnimGraph::BlendSpace1DBuilder("speed").sample(
        0.0f, AnimGraph::BlendSpace1DBuilder("missing").sample(0.0f, "a")))));
}

// Samples can be machines and blend spaces, built depth-first in position order like clips, and a machine sample's
// conditions read the graph's parameters.
void testBlendSpaceNestedLayout() {
    MachineBuilder nested;
    nested.state("b").state("c").transition(0, 1, { .condition = "y > 1" });
    MachineBuilder machine;
    machine.state(std::move(AnimGraph::BlendSpace1DBuilder("x")
                                .sample(2.0f, std::move(nested))
                                .sample(0.0f, "a")
                                .sample(1.0f, std::move(AnimGraph::BlendSpace1DBuilder("y")
                                                            .sample(1.0f, "e")
                                                            .sample(0.0f, "d")))));
    const AnimGraph graph = AnimGraph::Builder {}.parameter("x").parameter("y").root(std::move(machine)).build();

    REQUIRE_EQ(graph.nodeCount(), 9u);
    const auto& space = std::get<BlendSpace1DNode>(graph.node(NodeIndex { 1 }));
    REQUIRE_EQ(space.samples.size(), 3u);
    CHECK_EQ(space.samples[0].node.value, 2);
    CHECK_EQ(space.samples[1].node.value, 3);
    CHECK_EQ(space.samples[2].node.value, 6);
    CHECK_EQ(std::get<ClipNode>(graph.node(NodeIndex { 2 })).clip, std::string("a"));

    const auto* inner = std::get_if<BlendSpace1DNode>(&graph.node(NodeIndex { 3 }));
    REQUIRE(inner != nullptr);
    CHECK_EQ(inner->parameter, *graph.findParameter("y"));
    REQUIRE_EQ(inner->samples.size(), 2u);
    CHECK_EQ(std::get<ClipNode>(graph.node(inner->samples[0].node)).clip, std::string("d"));
    CHECK_EQ(std::get<ClipNode>(graph.node(inner->samples[1].node)).clip, std::string("e"));
    CHECK_EQ(inner->samples[1].node.value, 5);

    const auto* sampleMachine = std::get_if<StateMachineNode>(&graph.node(NodeIndex { 6 }));
    REQUIRE(sampleMachine != nullptr);
    CHECK_EQ(sampleMachine->states[0].node.value, 7);
    CHECK_EQ(sampleMachine->states[1].node.value, 8);
    CHECK(sampleMachine->transitionsFrom(0)[0].condition.has_value());
}

// The blend space is followed by its samples in position order, then by the next state.
void testBlendSpaceLayout() {
    MachineBuilder machine;
    machine.state(std::move(AnimGraph::BlendSpace1DBuilder("speed", 2.0f)
                                .sample(3.0f, "jog")
                                .sample(0.0f, "idle", false)
                                .sample(1.5f, "walk")))
        .state("jump");
    const AnimGraph graph = AnimGraph::Builder {}.parameter("x").parameter("speed").root(std::move(machine)).build();

    REQUIRE_EQ(graph.nodeCount(), 6u);
    const auto& root = std::get<StateMachineNode>(graph.node(AnimGraph::kRoot));
    REQUIRE_EQ(root.states[0].node.value, 1);
    CHECK_EQ(root.states[1].node.value, 5);

    const auto* space = std::get_if<BlendSpace1DNode>(&graph.node(NodeIndex { 1 }));
    REQUIRE(space != nullptr);
    CHECK_EQ(space->parameter, *graph.findParameter("speed"));
    CHECK_EQ(space->rate, 2.0f);
    REQUIRE_EQ(space->samples.size(), 3u);

    const char* names[] = { "idle", "walk", "jog" };
    const float positions[] = { 0.0f, 1.5f, 3.0f };
    for (size_t i = 0; i < 3; ++i) {
        CHECK_EQ(space->samples[i].node.value, i + 2);
        CHECK_EQ(space->samples[i].position, positions[i]);
        const auto& clip = std::get<ClipNode>(graph.node(space->samples[i].node));
        CHECK_EQ(clip.clip, std::string(names[i]));
        CHECK_EQ(clip.looping, i != 0);
    }
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

SyncTrack syncTrack(std::initializer_list<float> starts) {
    std::vector<SyncTrack::Marker> markers;
    uint32_t id = 1;
    for (float start : starts) {
        markers.push_back({ id++, start });
    }
    return SyncTrack { markers };
}

AnimGraph::Builder fadeGraph(bool synchronized) {
    return std::move(AnimGraph::Builder {}.parameter("go").root(
        twoStates({ .condition = "go", .fadeDuration = 1.0f, .synchronized = synchronized })));
}

// a (1 s) and b (2 s) both step at their start and halfway. Fading 0.3 s into a, b starts at a's sync time, and both
// then move at the pace of their blended duration.
void testSynchronizedFade() {
    Fixture fixture { "a", "b" };
    fixture.setDuration(1, 2.0f);
    for (auto& clip : fixture.clips) {
        clip->syncTrack = syncTrack({ 0.0f, 0.5f });
    }
    AnimGraphRunner runner = fixture.run(fadeGraph(true));

    runner.update(0.3f);
    runner.setFloat("go", 1.0f);
    runner.update(0.1f);
    REQUIRE_EQ(runner.currentState(), 1);

    // b weighs 0.1. Each step moves at the blended duration the fade built the step before: 1 s at weight 0 when it
    // started, then 1.1 s at weight 0.1.
    float percent = 0.3f + 0.1f / 1.0f;
    CHECK_NEAR(fixture.pose(runner, 0), 0.9f * percent, kEps);
    CHECK_NEAR(fixture.pose(runner, 1), 0.1f * percent * 2.0f, kEps);

    runner.update(0.5f);
    percent += 0.5f / 1.1f;
    CHECK_NEAR(fixture.pose(runner, 0), 0.4f * percent, kEps);
    CHECK_NEAR(fixture.pose(runner, 1), 0.6f * percent * 2.0f, kEps);
}

void testUnsynchronizedFade() {
    Fixture fixture { "a", "b" };
    fixture.setDuration(1, 2.0f);
    AnimGraphRunner runner = fixture.run(fadeGraph(false));

    runner.update(0.3f);
    runner.setFloat("go", 1.0f);
    runner.update(0.1f);
    CHECK_NEAR(fixture.pose(runner, 0), 0.9f * 0.4f, kEps);
    CHECK_NEAR(fixture.pose(runner, 1), 0.1f * 0.1f, kEps);
}

// a has two steps a second, b three in 1.5 s: their blended track has six events, and both clips are at the same event
// and the same percent through it, though their tracks differ.
void testSynchronizedEventCounts() {
    Fixture fixture { "a", "b" };
    fixture.setDuration(1, 1.5f);
    fixture.clips[0]->syncTrack = syncTrack({ 0.0f, 0.5f });
    fixture.clips[1]->syncTrack = syncTrack({ 0.0f, 1.0f / 3.0f, 2.0f / 3.0f });
    AnimGraphRunner runner = fixture.run(fadeGraph(true));

    runner.update(0.3f);
    runner.setFloat("go", 1.0f);
    runner.update(0.1f);
    runner.update(0.4f);

    const SyncTime a = fixture.clips[0]->syncTrack.time(fixture.pose(runner, 0) / 0.5f / 1.0f);
    const SyncTime b = fixture.clips[1]->syncTrack.time(fixture.pose(runner, 1) / 0.5f / 1.5f);
    CHECK_EQ(a.event, 1);
    CHECK_EQ(b.event, 1);
    CHECK_NEAR(a.percent, 0.6f, 1e-4f);
    CHECK_NEAR(b.percent, 0.6f, 1e-4f);
}

// The range reaches b through the machine it is in.
void testSynchronizedNestedMachine() {
    Fixture fixture { "a", "b" };
    fixture.setDuration(1, 2.0f);
    for (auto& clip : fixture.clips) {
        clip->syncTrack = syncTrack({ 0.0f, 0.5f });
    }
    MachineBuilder nested;
    nested.state("b");
    MachineBuilder machine;
    machine.state("a")
        .state(std::move(nested))
        .transition(0, 1, { .condition = "go", .fadeDuration = 1.0f, .synchronized = true });
    AnimGraphRunner runner = fixture.run(AnimGraph::Builder {}.parameter("go").root(std::move(machine)));

    runner.update(0.3f);
    runner.setFloat("go", 1.0f);
    runner.update(0.1f);
    const float percent = 0.3f + 0.1f / 1.0f;
    CHECK_NEAR(fixture.pose(runner, 0), 0.9f * percent, kEps);
    CHECK_NEAR(fixture.pose(runner, 1), 0.1f * percent * 2.0f, kEps);
}

// A fade into c interrupts the fade from a into b: the newer synchronized fade drives all three clips.
void testInterruptedSynchronizedFade() {
    Fixture fixture { "a", "b", "c" };
    for (auto& clip : fixture.clips) {
        clip->syncTrack = syncTrack({ 0.0f, 0.5f });
    }
    MachineBuilder machine;
    machine.state("a").state("b").state("c")
        .transition(0, 1, { .condition = "x == 1", .fadeDuration = 1.0f, .synchronized = true })
        .transition(1, 2, { .condition = "x == 2", .fadeDuration = 1.0f, .synchronized = true });
    AnimGraphRunner runner = fixture.run(AnimGraph::Builder {}.parameter("x").root(std::move(machine)));

    runner.update(0.3f);
    runner.setFloat("x", 1.0f);
    runner.update(0.1f);
    runner.setFloat("x", 2.0f);
    runner.update(0.1f);
    REQUIRE_EQ(runner.currentState(), 2);

    // b weighs 0.2 over a, and c 0.1 over both.
    CHECK_NEAR(fixture.pose(runner, 0), 0.8f * 0.9f * 0.5f, kEps);
    CHECK_NEAR(fixture.pose(runner, 1), 0.2f * 0.9f * 0.5f, kEps);
    CHECK_NEAR(fixture.pose(runner, 2), 0.1f * 0.5f, kEps);
}

// A synchronized fade from b into c starts while a plain fade from a markerless 4 s a into b is still running, like
// walk into jog while idle into walk is. It keeps b's and c's pace, and a keeps its own time.
void testSynchronizedOverUnsynchronizedFade() {
    Fixture fixture { "a", "b", "c" };
    fixture.setDuration(0, 4.0f);
    fixture.setDuration(2, 2.0f);
    fixture.clips[1]->syncTrack = syncTrack({ 0.0f, 0.5f });
    fixture.clips[2]->syncTrack = syncTrack({ 0.0f, 0.5f });
    MachineBuilder machine;
    machine.state("a").state("b").state("c")
        .transition(0, 1, { .condition = "x == 1", .fadeDuration = 1.0f })
        .transition(1, 2, { .condition = "x == 2", .fadeDuration = 1.0f, .synchronized = true });
    AnimGraphRunner runner = fixture.run(AnimGraph::Builder {}.parameter("x").root(std::move(machine)));

    runner.update(0.3f);
    runner.setFloat("x", 1.0f);
    runner.update(0.1f);
    runner.setFloat("x", 2.0f);
    runner.update(0.1f);
    REQUIRE_EQ(runner.currentState(), 2);

    // c starts 0.1 into its loop, where b is, and both move at b's 1 s. b weighs 0.2 over a, and c 0.1 over both.
    float percent = 0.1f + 0.1f / 1.0f;
    CHECK_NEAR(fixture.pose(runner, 0), 0.8f * 0.9f * 0.5f, kEps);
    CHECK_NEAR(fixture.pose(runner, 1), 0.2f * 0.9f * percent, kEps);
    CHECK_NEAR(fixture.pose(runner, 2), 0.1f * percent * 2.0f, kEps);

    // The pace is now 1.1 s, b's and c's durations blended at 0.1.
    runner.update(0.5f);
    percent += 0.5f / 1.1f;
    CHECK_NEAR(fixture.pose(runner, 0), 0.3f * 0.4f * 1.0f, kEps);
    CHECK_NEAR(fixture.pose(runner, 1), 0.7f * 0.4f * percent, kEps);
    CHECK_NEAR(fixture.pose(runner, 2), 0.6f * percent * 2.0f, kEps);
}

// Joining a's loop 0.9 s in, a non-looping b plays to its end with a and then stays there as a wraps around.
void testSynchronizedNonLooping() {
    Fixture fixture { "a", "b" };
    for (auto& clip : fixture.clips) {
        clip->syncTrack = syncTrack({ 0.0f, 0.5f });
    }
    MachineBuilder machine;
    machine.state("a").state("b", false)
        .transition(0, 1, { .condition = "go", .fadeDuration = 1.0f, .synchronized = true });
    AnimGraphRunner runner = fixture.run(AnimGraph::Builder {}.parameter("go").root(std::move(machine)));

    runner.update(0.9f);
    runner.setFloat("go", 1.0f);
    runner.update(0.05f);
    CHECK_NEAR(fixture.pose(runner, 1), 0.05f * 0.95f, kEps);
    runner.update(0.15f);
    CHECK_NEAR(fixture.pose(runner, 0), 0.8f * 0.1f, kEps);
    CHECK_NEAR(fixture.pose(runner, 1), 0.2f * 1.0f, kEps);
}

// Fading back into a before its fade out has finished: a keeps its time instead of starting over, which would make it
// jump where it is fading out as well.
void testReenteredStateContinues() {
    Fixture fixture { "a", "b" };
    MachineBuilder machine;
    machine.state("a").state("b")
        .transition(0, 1, { .condition = "x == 1", .fadeDuration = 1.0f })
        .transition(1, 0, { .condition = "x == 0", .fadeDuration = 1.0f });
    AnimGraphRunner runner = fixture.run(AnimGraph::Builder {}.parameter("x").root(std::move(machine)));

    runner.update(0.3f);
    runner.setFloat("x", 1.0f);
    runner.update(0.1f);
    runner.setFloat("x", 0.0f);
    runner.update(0.1f);
    REQUIRE_EQ(runner.currentState(), 0);

    // b weighs 0.2 over a, then a 0.1 over both, with a at 0.5 s and b at 0.2 s.
    CHECK_NEAR(fixture.pose(runner, 0), (0.9f * 0.8f + 0.1f) * 0.5f, kEps);
    CHECK_NEAR(fixture.pose(runner, 1), 0.9f * 0.2f * 0.2f, kEps);
}

void testSelfTransitionContinues() {
    Fixture fixture { "a" };
    MachineBuilder machine;
    machine.state("a").transition(0, 0, { .trigger = "again", .fadeDuration = 1.0f });
    AnimGraphRunner runner = fixture.run(AnimGraph::Builder {}.root(std::move(machine)));

    runner.update(0.3f);
    runner.setTrigger("again");
    runner.update(0.1f);
    CHECK_NEAR(fixture.pose(runner, 0), 0.4f, kEps);
}

// The root is started when the runner is made, so a reversed clip is at its end before the first update.
void testStartedBeforeUpdate() {
    Fixture fixture { "a" };
    fixture.setDuration(0, 2.0f);
    AnimGraphRunner runner = fixture.run(oneState(true, -1.0f));
    CHECK_NEAR(fixture.pose(runner, 0), 2.0f, kEps);
}

AnimGraph::Builder blendSpaceGraph(float x, AnimGraph::BlendSpace1DBuilder blendSpace) {
    MachineBuilder machine;
    machine.state(std::move(blendSpace));
    return std::move(AnimGraph::Builder {}.parameter("x", x).root(std::move(machine)));
}

AnimGraph::BlendSpace1DBuilder abBlendSpace(float rate = 1.0f) {
    return std::move(AnimGraph::BlendSpace1DBuilder("x", rate).sample(0.0f, "a").sample(1.0f, "b"));
}

void setSyncTracks(Fixture& fixture, std::initializer_list<float> starts) {
    for (auto& clip : fixture.clips) {
        clip->syncTrack = syncTrack(starts);
    }
}

// Outside the samples' range only the end sample plays. One that comes back into play starts at the blend's sync
// time.
void testBlendSpaceWeights() {
    Fixture fixture { "a", "b" };
    AnimGraphRunner runner = fixture.run(blendSpaceGraph(0.25f, abBlendSpace()));

    runner.update(0.4f);
    CHECK_NEAR(fixture.pose(runner, 0), 0.75f * 0.4f, kEps);
    CHECK_NEAR(fixture.pose(runner, 1), 0.25f * 0.4f, kEps);

    runner.setFloat("x", -1.0f);
    runner.update(0.1f);
    CHECK_NEAR(fixture.pose(runner, 0), 0.5f, kEps);
    CHECK_NEAR(fixture.pose(runner, 1), 0.0f, kEps);

    runner.setFloat("x", 5.0f);
    runner.update(0.1f);
    CHECK_NEAR(fixture.pose(runner, 0), 0.0f, kEps);
    CHECK_NEAR(fixture.pose(runner, 1), 0.6f, kEps);
}

void testBlendSpaceRate() {
    Fixture fixture { "a", "b" };
    AnimGraphRunner runner = fixture.run(blendSpaceGraph(0.0f, abBlendSpace(2.0f)));
    runner.update(0.1f);
    CHECK_NEAR(fixture.pose(runner, 0), 0.2f, kEps);
}

// a (1 s) and b (2 s) halfway blend into a 1.5 s loop, in which both are at the same sync time.
void testBlendSpaceSynchronized() {
    Fixture fixture { "a", "b" };
    fixture.setDuration(1, 2.0f);
    setSyncTracks(fixture, { 0.0f, 0.5f });
    AnimGraphRunner runner = fixture.run(blendSpaceGraph(0.5f, abBlendSpace()));

    runner.update(0.3f);
    const float percent = 0.3f / 1.5f;
    CHECK_NEAR(fixture.pose(runner, 0), 0.5f * percent, kEps);
    CHECK_NEAR(fixture.pose(runner, 1), 0.5f * percent * 2.0f, kEps);
}

// a has two steps a second and b three in 1.5 s, so the blend has six events of 1/6 each and lasts 3 s. 0.7 s in, it
// is 0.4 through its second event, and so are both clips.
void testBlendSpaceEventCounts() {
    Fixture fixture { "a", "b" };
    fixture.setDuration(1, 1.5f);
    fixture.clips[0]->syncTrack = syncTrack({ 0.0f, 0.5f });
    fixture.clips[1]->syncTrack = syncTrack({ 0.0f, 1.0f / 3.0f, 2.0f / 3.0f });
    AnimGraphRunner runner = fixture.run(blendSpaceGraph(0.5f, abBlendSpace()));

    runner.update(0.7f);
    const SyncTime a = fixture.clips[0]->syncTrack.time(fixture.pose(runner, 0) / 0.5f / 1.0f);
    const SyncTime b = fixture.clips[1]->syncTrack.time(fixture.pose(runner, 1) / 0.5f / 1.5f);
    CHECK_EQ(a.event, 1);
    CHECK_EQ(b.event, 1);
    CHECK_NEAR(a.percent, 0.4f, 1e-4f);
    CHECK_NEAR(b.percent, 0.4f, 1e-4f);
}

// Moving the parameter brings b in at a's sync time, and then both move at the pace of the new blend.
void testBlendSpaceParameterChange() {
    Fixture fixture { "a", "b" };
    fixture.setDuration(1, 2.0f);
    setSyncTracks(fixture, { 0.0f, 0.5f });
    AnimGraphRunner runner = fixture.run(blendSpaceGraph(0.0f, abBlendSpace()));

    runner.update(0.3f);
    CHECK_NEAR(fixture.pose(runner, 0), 0.3f, kEps);

    runner.setFloat("x", 0.5f);
    runner.update(0.15f);
    const float percent = 0.3f + 0.15f / 1.5f;
    CHECK_NEAR(fixture.pose(runner, 0), 0.5f * percent, kEps);
    CHECK_NEAR(fixture.pose(runner, 1), 0.5f * percent * 2.0f, kEps);
}

// Past b, the blend moves from a and b to b and c: b keeps going, and c starts at its sync time.
void testBlendSpaceCrossesSample() {
    Fixture fixture { "a", "b", "c" };
    fixture.setDuration(1, 2.0f);
    fixture.setDuration(2, 4.0f);
    setSyncTracks(fixture, { 0.0f, 0.5f });
    AnimGraphRunner runner = fixture.run(blendSpaceGraph(0.5f, std::move(abBlendSpace().sample(2.0f, "c"))));

    runner.update(0.3f);
    float percent = 0.3f / 1.5f;
    CHECK_NEAR(fixture.pose(runner, 0), 0.5f * percent, kEps);
    CHECK_NEAR(fixture.pose(runner, 1), 0.5f * percent * 2.0f, kEps);

    runner.setFloat("x", 1.5f);
    runner.update(0.3f);
    percent += 0.3f / 3.0f;
    CHECK_NEAR(fixture.pose(runner, 0), 0.0f, kEps);
    CHECK_NEAR(fixture.pose(runner, 1), 0.5f * percent * 2.0f, kEps);
    CHECK_NEAR(fixture.pose(runner, 2), 0.5f * percent * 4.0f, kEps);
}

// A non-looping sample stops at its end when the blend loops, while the looping one carries on.
void testBlendSpaceNonLoopingSample() {
    Fixture fixture { "a", "b" };
    AnimGraphRunner runner = fixture.run(blendSpaceGraph(
        0.5f, std::move(AnimGraph::BlendSpace1DBuilder("x").sample(0.0f, "a", false).sample(1.0f, "b"))));

    runner.update(0.8f);
    runner.update(0.4f);
    CHECK_NEAR(fixture.pose(runner, 0), 0.5f * 1.0f, kEps);
    CHECK_NEAR(fixture.pose(runner, 1), 0.5f * 0.2f, kEps);
}

// A synchronized fade from c into the blend space starts both samples at c's sync time, and the fade's range drives
// them.
void testSynchronizedFadeIntoBlendSpace() {
    Fixture fixture { "a", "b", "c" };
    fixture.setDuration(1, 2.0f);
    setSyncTracks(fixture, { 0.0f, 0.5f });
    MachineBuilder machine;
    machine.state("c")
        .state(abBlendSpace())
        .transition(0, 1, { .condition = "go", .fadeDuration = 1.0f, .synchronized = true });
    AnimGraphRunner runner =
        fixture.run(AnimGraph::Builder {}.parameter("go").parameter("x", 0.5f).root(std::move(machine)));

    runner.update(0.3f);
    runner.setFloat("go", 1.0f);
    runner.update(0.1f);
    REQUIRE_EQ(runner.currentState(), 1);

    const float percent = 0.3f + 0.1f / 1.0f;
    CHECK_NEAR(fixture.pose(runner, 2), 0.9f * percent, kEps);
    CHECK_NEAR(fixture.pose(runner, 0), 0.1f * 0.5f * percent, kEps);
    CHECK_NEAR(fixture.pose(runner, 1), 0.1f * 0.5f * percent * 2.0f, kEps);
}


// A machine sample that leaves the pair and comes back keeps the state it was in, and the blend's range moves it into
// step: c, entered while the machine was in play, is 0.3 s in, the blend's time.
void testBlendSpaceMachineSampleRejoins() {
    Fixture fixture { "a", "b", "c" };
    MachineBuilder machine;
    machine.state("b").state("c").transition(0, 1, { .condition = "go" });
    AnimGraphRunner runner = fixture.run(AnimGraph::Builder {}.parameter("go").parameter("x", 1.0f).root(
        std::move(MachineBuilder {}.state(
            std::move(AnimGraph::BlendSpace1DBuilder("x").sample(0.0f, "a").sample(1.0f, std::move(machine)))))));
    const NodeIndex sample { 3 };

    runner.setFloat("go", 1.0f);
    runner.update(0.1f);
    REQUIRE_EQ(runner.currentState(sample), 1);

    runner.setFloat("go", 0.0f);
    runner.setFloat("x", 0.0f);
    runner.update(0.1f);
    CHECK_NEAR(fixture.pose(runner, 0), 0.2f, kEps);

    runner.setFloat("x", 1.0f);
    runner.update(0.1f);
    CHECK_EQ(runner.currentState(sample), 1);
    CHECK_NEAR(fixture.pose(runner, 0), 0.0f, kEps);
    CHECK_NEAR(fixture.pose(runner, 1), 0.0f, kEps);
    CHECK_NEAR(fixture.pose(runner, 2), 0.3f, kEps);
}

// b (2 s) and c (4 s) halfway blend into 3 s, and that with a (1 s) halfway into 2 s, so 0.4 s in, all three are 0.2
// through their loops.
void testNestedBlendSpaceSynchronized() {
    Fixture fixture { "a", "b", "c" };
    fixture.setDuration(1, 2.0f);
    fixture.setDuration(2, 4.0f);
    setSyncTracks(fixture, { 0.0f, 0.5f });
    AnimGraph::BlendSpace1DBuilder inner("y");
    inner.sample(0.0f, "b").sample(1.0f, "c");
    AnimGraph::BlendSpace1DBuilder outer("x");
    outer.sample(0.0f, "a").sample(1.0f, std::move(inner));
    AnimGraphRunner runner = fixture.run(AnimGraph::Builder {}.parameter("x", 0.5f).parameter("y", 0.5f).root(
        std::move(MachineBuilder {}.state(std::move(outer)))));

    runner.update(0.4f);
    CHECK_NEAR(fixture.pose(runner, 0), 0.5f * 0.2f * 1.0f, kEps);
    CHECK_NEAR(fixture.pose(runner, 1), 0.25f * 0.2f * 2.0f, kEps);
    CHECK_NEAR(fixture.pose(runner, 2), 0.25f * 0.2f * 4.0f, kEps);
}

}

void runAnimGraphTests() {
    ailo::test::run("anim_graph/testBuildErrors", testBuildErrors);
    ailo::test::run("anim_graph/testBlendSpaceLayout", testBlendSpaceLayout);
    ailo::test::run("anim_graph/testBlendSpaceNestedLayout", testBlendSpaceNestedLayout);
    ailo::test::run("anim_graph/testParameters", testParameters);
    ailo::test::run("anim_graph/testConditionGatesTransition", testConditionGatesTransition);
    ailo::test::run("anim_graph/testTriggerKeptWhenConditionFails", testTriggerKeptWhenConditionFails);
    ailo::test::run("anim_graph/testNestedMachineReadsParameters", testNestedMachineReadsParameters);
    ailo::test::run("anim_graph/testClipTime", testClipTime);
    ailo::test::run("anim_graph/testSynchronizedFade", testSynchronizedFade);
    ailo::test::run("anim_graph/testUnsynchronizedFade", testUnsynchronizedFade);
    ailo::test::run("anim_graph/testSynchronizedEventCounts", testSynchronizedEventCounts);
    ailo::test::run("anim_graph/testSynchronizedNestedMachine", testSynchronizedNestedMachine);
    ailo::test::run("anim_graph/testInterruptedSynchronizedFade", testInterruptedSynchronizedFade);
    ailo::test::run("anim_graph/testSynchronizedOverUnsynchronizedFade", testSynchronizedOverUnsynchronizedFade);
    ailo::test::run("anim_graph/testSynchronizedNonLooping", testSynchronizedNonLooping);
    ailo::test::run("anim_graph/testReenteredStateContinues", testReenteredStateContinues);
    ailo::test::run("anim_graph/testSelfTransitionContinues", testSelfTransitionContinues);
    ailo::test::run("anim_graph/testStartedBeforeUpdate", testStartedBeforeUpdate);
    ailo::test::run("anim_graph/testBlendSpaceWeights", testBlendSpaceWeights);
    ailo::test::run("anim_graph/testBlendSpaceRate", testBlendSpaceRate);
    ailo::test::run("anim_graph/testBlendSpaceSynchronized", testBlendSpaceSynchronized);
    ailo::test::run("anim_graph/testBlendSpaceEventCounts", testBlendSpaceEventCounts);
    ailo::test::run("anim_graph/testBlendSpaceParameterChange", testBlendSpaceParameterChange);
    ailo::test::run("anim_graph/testBlendSpaceCrossesSample", testBlendSpaceCrossesSample);
    ailo::test::run("anim_graph/testBlendSpaceNonLoopingSample", testBlendSpaceNonLoopingSample);
    ailo::test::run("anim_graph/testSynchronizedFadeIntoBlendSpace", testSynchronizedFadeIntoBlendSpace);
    ailo::test::run("anim_graph/testBlendSpaceMachineSampleRejoins", testBlendSpaceMachineSampleRejoins);
    ailo::test::run("anim_graph/testNestedBlendSpaceSynchronized", testNestedBlendSpaceSynchronized);
}
