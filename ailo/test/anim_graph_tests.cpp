#include "ecs/AnimGraph.h"
#include "Testing.h"

#include <string>
#include <vector>

using namespace ailo;

namespace {

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

void testBuildErrors() {
    {
        AnimGraph::Builder builder;
        builder.state("a").state("b").transition(0, 1, { .condition = "missing > 0" });
        CHECK_THROWS(builder.build());
    }
    {
        AnimGraph::Builder builder;
        builder.parameter("speed").state("a").state("b").transition(0, 1, { .condition = "speed >" });
        CHECK_THROWS(builder.build());
    }
    {
        AnimGraph::Builder nested;
        nested.parameter("speed", 2.0f).state("b");
        AnimGraph::Builder builder;
        builder.parameter("speed", 1.0f).state("a").state(std::move(nested));
        CHECK_THROWS(builder.build());
    }
}

void testParameters() {
    AnimGraph::Builder nested;
    nested.parameter("speed", 1.5f).state("b");
    AnimGraph::Builder builder;
    builder.parameter("speed", 1.5f)
        .parameter("grounded", 1.0f)
        .state("a")
        .state(std::move(nested))
        .transition(0, 1, { .condition = "1 < 2" })
        .transition(1, 0, { .condition = "1 > 2" });
    const AnimGraph graph = builder.build();

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
    AnimGraph::Builder builder;
    builder.parameter("speed").state("a").state("b").transition(0, 1, { .condition = "speed > 1" });
    AnimGraphRunner runner = fixture.run(builder);

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
    AnimGraph::Builder builder;
    builder.parameter("ready").state("a").state("b").transition(0, 1, { .trigger = "go", .condition = "ready" });
    AnimGraphRunner runner = fixture.run(builder);

    runner.setTrigger("go");
    runner.update(0.1f);
    CHECK_EQ(runner.currentState(), 0);

    runner.setFloat("ready", 1.0f);
    runner.update(0.1f);
    CHECK_EQ(runner.currentState(), 1);
}

void testNestedMachineReadsRootParameter() {
    Fixture fixture { "a", "b" };
    AnimGraph::Builder nested;
    nested.state("a").state("b").transition(0, 1, { .condition = "x > 0 && y" });
    AnimGraph::Builder builder;
    builder.parameter("x").parameter("y", 1.0f).state(std::move(nested));
    const AssetPtr<AnimGraph> graph = fixture.graphStorage.emplace(noname_t {}, builder.build());
    AnimGraphRunner runner(graph, fixture.clips);

    const NodeIndex machine { 1 };
    runner.update(0.1f);
    CHECK_EQ(runner.currentState(machine), 0);

    const auto x = graph->findParameter("x");
    REQUIRE(x.has_value());
    runner.setFloat(*x, 3.0f);
    runner.update(0.1f);
    CHECK_EQ(runner.currentState(machine), 1);
}

}

void runAnimGraphTests() {
    ailo::test::run("anim_graph/testBuildErrors", testBuildErrors);
    ailo::test::run("anim_graph/testParameters", testParameters);
    ailo::test::run("anim_graph/testConditionGatesTransition", testConditionGatesTransition);
    ailo::test::run("anim_graph/testTriggerKeptWhenConditionFails", testTriggerKeptWhenConditionFails);
    ailo::test::run("anim_graph/testNestedMachineReadsRootParameter", testNestedMachineReadsRootParameter);
}
