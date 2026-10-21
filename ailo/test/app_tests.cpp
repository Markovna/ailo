#include "app/App.h"
#include "ecs/Transform.h"
#include "Testing.h"

#include <iostream>
#include <string>
#include <vector>

using namespace ailo;

namespace {

std::vector<std::string> g_log;

struct Counter { int value = 0; };
struct Time { float delta = 0.016f; float elapsed = 0.0f; };
struct Position { float x = 0; };
struct Velocity { float dx = 0; };

// Non-movable, non-copyable: resources are constructed in place.
struct Device {
    explicit Device(std::string name) : name(std::move(name)) { g_log.push_back("Device()"); }
    ~Device() { g_log.push_back("~Device()"); }
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;
    std::string name;
};

struct Renderer {
    explicit Renderer(Device& device) : device(device) { g_log.push_back("Renderer()"); }
    ~Renderer() { g_log.push_back("~Renderer()"); }
    Device& device;
};

void tickTime(Time& time) { time.elapsed += time.delta; }

void move(Query<Position, Velocity> query, const Time& time) {
    for (auto [entity, pos, vel] : query.each()) {
        pos.x += vel.dx * time.delta;
    }
}

struct Frozen {};

void moveUnfrozen(Query<Position, Velocity, Exclude<Frozen>> query) {
    for (auto [entity, pos, vel] : query.each()) {
        pos.x += vel.dx;
    }
}

void testQueryExclude() {
    App app;
    Entity moving {};
    Entity frozen {};
    app.addSystem(Stage::Startup, [&](World& world) {
           auto& scene = world.scene();
           moving = scene.addEntity();
           scene.addComponent<Position>(moving);
           scene.addComponent<Velocity>(moving, 1.0f);
           frozen = scene.addEntity();
           scene.addComponent<Position>(frozen);
           scene.addComponent<Velocity>(frozen, 1.0f);
           scene.addComponent<Frozen>(frozen);
       })
       .addSystem(Stage::Update, moveUnfrozen)
       .addSystem(Stage::Update, [&](const Query<Position, Exclude<Frozen>, Exclude<Velocity>>& query, AppControl& control) {
           CHECK(!query.contains(moving));
           CHECK(!query.contains(frozen));
           control.requestExit();
       });
    app.run();

    auto& scene = app.world().scene();
    CHECK_EQ(scene.registry().get<Position>(moving).x, 1.0f);
    CHECK_EQ(scene.registry().get<Position>(frozen).x, 0.0f);
}

void testWorldResources() {
    World world;
    auto& counter = world.insertResource<Counter>(5);
    CHECK_EQ(counter.value, 5);
    CHECK(world.hasResource<Counter>());
    CHECK(!world.hasResource<Time>());
    CHECK_EQ(world.tryResource<Time>(), nullptr);

    auto* before = &world.resource<Counter>();
    world.insertResource<Position>();
    world.insertResource<Velocity>();
    world.insertResource<std::vector<int>>();
    CHECK_EQ(before, &world.resource<Counter>());  // references stay stable across inserts

    bool threw = false;
    try { world.insertResource<Counter>(); } catch (const std::runtime_error&) { threw = true; }
    CHECK(threw);

    threw = false;
    try { world.resource<Time>(); } catch (const std::runtime_error&) { threw = true; }
    CHECK(threw);
}

void testDestructionOrder() {
    g_log.clear();
    {
        World world;
        auto& device = world.insertResource<Device>("gpu");
        world.insertResource<Renderer>(device);

        auto entity = world.scene().addEntity();
        world.scene().addComponent<Position>(entity);
        world.scene().onDestroy<Position>().connect<+[](entt::registry&, entt::entity) {
            g_log.push_back("~Position");
        }>();
    }
    const std::vector<std::string> expected { "Device()", "Renderer()", "~Position", "~Renderer()", "~Device()" };
    CHECK_EQ(g_log, expected);
}

void testSystemsAndStages() {
    App app;
    app.insertResource<Time>();
    app.insertResource<Counter>();

    std::vector<std::string> order;
    app.addSystem(Stage::Last, [&order] { order.push_back("Last"); })
       .addSystem(Stage::First, tickTime)
       .addSystem(Stage::First, [&order] { order.push_back("First"); })
       .addSystem(Stage::Update, move)
       .addSystem(Stage::Update, [&order](Counter& c, AppControl& control) {
           order.push_back("Update");
           if (++c.value == 3) control.requestExit();
       })
       .addSystem(Stage::Startup, [&order](World& world) {
           order.push_back("Startup");
           auto e = world.scene().addEntity();
           world.scene().addComponent<Position>(e);
           world.scene().addComponent<Velocity>(e, 10.0f);
       })
       .addSystem(Stage::Shutdown, [&order](const Counter* c, Device* optional) {
           CHECK(c && c->value == 3);
           CHECK_EQ(optional, nullptr);
           order.push_back("Shutdown");
       });

    app.run();

    CHECK_EQ(app.resource<Counter>().value, 3);
    const std::vector<std::string> expected {
        "Startup",
        "First", "Update", "Last",
        "First", "Update", "Last",
        "First", "Update", "Last",
        "Shutdown"
    };
    CHECK_EQ(order, expected);

    auto& time = app.resource<Time>();
    for (auto [e, pos] : app.world().scene().view<Position>().each()) {
        CHECK_NEAR(pos.x, 10.0f * time.delta * 3, 1e-4f);
    }
}

void testStartupCanInsertResources() {
    App app;
    int seen = 0;
    app.addSystem(Stage::Startup, [](World& world) { world.insertResource<Counter>(42); })
       .addSystem(Stage::Update, [&seen](Counter& c, AppControl& control) { seen = c.value; control.requestExit(); });
    app.run();
    CHECK_EQ(seen, 42);
}

void testMissingResourceIsReported() {
    App app;
    app.addSystem(Stage::Update, [](Counter&, const Time&) {}, "needsStuff");

    std::string message;
    try { app.run(); } catch (const std::runtime_error& e) { message = e.what(); }
    CHECK_NE(message.find("needsStuff"), std::string::npos);
    CHECK_NE(message.find("Counter"), std::string::npos);   // first missing param only
    CHECK_EQ(message.find("Time"), std::string::npos);
}

struct CounterPlugin {
    int start = 0;
    int builds = 0;

    void build(App& app) {
        builds++;
        app.insertResource<Counter>(start);
        // Plugins are kept alive by the App, so capturing `this` is safe.
        app.addSystem(Stage::Update, [this](Counter& c, AppControl& control) {
            c.value += 1;
            if (c.value >= start + 2) control.requestExit();
        });
    }
};

struct DependentPlugin {
    void build(App& app) {
        app.addPlugin(CounterPlugin { .start = 100 });   // dependency; ignored if already added
        app.addSystem(Stage::Last, [](Counter& c) { c.value += 1000; });
    }
};

void testPlugins() {
    App app;
    app.addPlugin(CounterPlugin { .start = 10 })
       .addPlugin(DependentPlugin {})
       .addPlugin(CounterPlugin { .start = 99 });   // duplicate, ignored

    CHECK(app.hasPlugin<CounterPlugin>());
    CHECK(app.hasPlugin<DependentPlugin>());

    app.run();
    // frame 1: 10 → 11 → 1011; frame 2: 1012 ≥ 12 → exit, then Last → 2012
    CHECK_EQ(app.resource<Counter>().value, 2012);
}

void testCannotAddSystemsWhileRunning() {
    App app;
    bool threw = false;
    app.addSystem(Stage::Startup, [&app, &threw] {
        try { app.addSystem(Stage::Update, [] {}); } catch (const std::logic_error&) { threw = true; }
    });
    app.addSystem(Stage::First, [](AppControl& c) { c.requestExit(); });
    app.run();
    CHECK(threw);
}

void testAppTeardownOrder() {
    g_log.clear();
    {
        App app;
        auto& device = app.insertResource<Device>("gpu");
        app.insertResource<Renderer>(device);
        app.addSystem(Stage::First, [](AppControl& c) { c.requestExit(); });
        app.run();
    }
    const std::vector<std::string> expected { "Device()", "Renderer()", "~Renderer()", "~Device()" };
    CHECK_EQ(g_log, expected);
}

void testShutdownRunsInReverseOrder() {
    std::vector<std::string> order;
    App app;
    app.addSystem(Stage::Shutdown, [&order] { order.push_back("first registered"); })
       .addSystem(Stage::Shutdown, [&order] { order.push_back("second registered"); })
       .addSystem(Stage::First, [](AppControl& c) { c.requestExit(); });
    app.run();
    const std::vector<std::string> expected { "second registered", "first registered" };
    CHECK_EQ(order, expected);
}

void testFixedUpdate() {
    App app;
    auto& fixed = app.resource<FixedTime>();
    fixed.step = 0.25f;
    fixed.maxDelta = 1.0f;

    const std::array deltas { 0.625f, 0.125f, 0.25f, 10.0f };
    std::vector<std::string> order;
    std::vector<int> stepsPerFrame;
    std::vector<float> alphas;
    int frame = 0;

    app.addSystem(Stage::First, [&](FixedTime& f) {
           f.accumulate(deltas[frame]);
           stepsPerFrame.push_back(0);
       })
       .addSystem(Stage::Update, [&order] { order.push_back("Update"); })
       .addSystem(Stage::FixedUpdate, [&](const FixedTime& f) {
           order.push_back("Fixed");
           stepsPerFrame.back()++;
           CHECK(f.alpha >= 0.0f && f.alpha < 1.0f);
       })
       .addSystem(Stage::PostUpdate, [&order] { order.push_back("PostUpdate"); })
       .addSystem(Stage::Last, [&](const FixedTime& f, AppControl& control) {
           alphas.push_back(f.alpha);
           if (++frame == static_cast<int>(deltas.size())) control.requestExit();
       });
    app.run();

    // 0.625 → 2 steps (0.125 left); +0.125 → 1 step; +0.25 → 1 step; 10 clamped to 1 → 4 steps.
    CHECK((stepsPerFrame == std::vector { 2, 1, 1, 4 }));
    CHECK((alphas == std::vector { 0.5f, 0.0f, 0.0f, 0.0f }));
    CHECK_EQ(fixed.tick, 8);
    CHECK_EQ(fixed.elapsed, 2.0f);

    const std::vector<std::string> firstFrames {
        "Update", "Fixed", "Fixed", "PostUpdate",
        "Update", "Fixed", "PostUpdate",
    };
    REQUIRE(order.size() >= firstFrames.size());
    CHECK(std::equal(firstFrames.begin(), firstFrames.end(), order.begin()));
}

void testFixedUpdateCanRunZeroTimes() {
    App app;
    auto& fixed = app.resource<FixedTime>();
    fixed.step = 0.5f;
    int steps = 0;
    app.addSystem(Stage::First, [](FixedTime& f) { f.accumulate(0.25f); })
       .addSystem(Stage::FixedUpdate, [&steps] { steps++; })
       .addSystem(Stage::Last, [](AppControl& control) { control.requestExit(); });
    app.run();
    CHECK_EQ(steps, 0);
    CHECK_EQ(fixed.alpha, 0.5f);
}

}

void runAppTests() {
    ailo::test::run("app/testWorldResources", testWorldResources);
    ailo::test::run("app/testDestructionOrder", testDestructionOrder);
    ailo::test::run("app/testSystemsAndStages", testSystemsAndStages);
    ailo::test::run("app/testStartupCanInsertResources", testStartupCanInsertResources);
    ailo::test::run("app/testMissingResourceIsReported", testMissingResourceIsReported);
    ailo::test::run("app/testPlugins", testPlugins);
    ailo::test::run("app/testCannotAddSystemsWhileRunning", testCannotAddSystemsWhileRunning);
    ailo::test::run("app/testAppTeardownOrder", testAppTeardownOrder);
    ailo::test::run("app/testShutdownRunsInReverseOrder", testShutdownRunsInReverseOrder);
    ailo::test::run("app/testFixedUpdate", testFixedUpdate);
    ailo::test::run("app/testFixedUpdateCanRunZeroTimes", testFixedUpdateCanRunZeroTimes);
    ailo::test::run("app/testQueryExclude", testQueryExclude);
}
