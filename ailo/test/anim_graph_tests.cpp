#include "ecs/AnimGraph.h"
#include "Testing.h"

#include <string>
#include <vector>

using namespace ailo;

namespace {

using MachineBuilder = AnimGraph::MachineBuilder;

struct Fixture {
    AssetStorage<AnimationClip> clipStorage;
    AssetStorage<AnimGraph> graphStorage;
    std::vector<AssetPtr<AnimationClip>> clips;

    explicit Fixture(std::initializer_list<const char*> names) {
        for (const char* name : names) {
            clips.push_back(clipStorage.emplace(noname_t {}, AnimationClip { .name = name, .duration = 1.0f }));
        }
    }

    AnimGraphRunner run(const AnimGraph::Builder& builder) {
        return AnimGraphRunner(graphStorage.emplace(noname_t {}, builder.build()), clips);
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

}

void runAnimGraphTests() {
    ailo::test::run("anim_graph/testBuildErrors", testBuildErrors);
    ailo::test::run("anim_graph/testParameters", testParameters);
    ailo::test::run("anim_graph/testConditionGatesTransition", testConditionGatesTransition);
    ailo::test::run("anim_graph/testTriggerKeptWhenConditionFails", testTriggerKeptWhenConditionFails);
    ailo::test::run("anim_graph/testNestedMachineReadsParameters", testNestedMachineReadsParameters);
}
