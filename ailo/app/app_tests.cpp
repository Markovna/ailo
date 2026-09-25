#include "App.h"

#include <cassert>
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

void testWorldResources() {
    World world;
    auto& counter = world.insertResource<Counter>(5);
    assert(counter.value == 5);
    assert(world.hasResource<Counter>());
    assert(!world.hasResource<Time>());
    assert(world.tryResource<Time>() == nullptr);

    auto* before = &world.resource<Counter>();
    world.insertResource<Position>();
    world.insertResource<Velocity>();
    world.insertResource<std::vector<int>>();
    assert(before == &world.resource<Counter>());  // references stay stable across inserts

    bool threw = false;
    try { world.insertResource<Counter>(); } catch (const std::runtime_error&) { threw = true; }
    assert(threw);

    threw = false;
    try { world.resource<Time>(); } catch (const std::runtime_error&) { threw = true; }
    assert(threw);
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
    assert(g_log == expected);
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
           assert(c && c->value == 3);
           assert(optional == nullptr);
           order.push_back("Shutdown");
       });

    app.run();

    assert(app.resource<Counter>().value == 3);
    const std::vector<std::string> expected {
        "Startup",
        "First", "Update", "Last",
        "First", "Update", "Last",
        "First", "Update", "Last",
        "Shutdown"
    };
    assert(order == expected);

    auto& time = app.resource<Time>();
    for (auto [e, pos] : app.world().scene().view<Position>().each()) {
        assert(std::abs(pos.x - 10.0f * time.delta * 3) < 1e-4f);
    }
}

void testStartupCanInsertResources() {
    App app;
    int seen = 0;
    app.addSystem(Stage::Startup, [](World& world) { world.insertResource<Counter>(42); })
       .addSystem(Stage::Update, [&seen](Counter& c, AppControl& control) { seen = c.value; control.requestExit(); });
    app.run();
    assert(seen == 42);
}

void testMissingResourceIsReported() {
    App app;
    app.addSystem(Stage::Update, [](Counter&, const Time&) {}, "needsStuff");

    std::string message;
    try { app.run(); } catch (const std::runtime_error& e) { message = e.what(); }
    assert(message.find("needsStuff") != std::string::npos);
    assert(message.find("Counter") != std::string::npos);   // first missing param only
    assert(message.find("Time") == std::string::npos);
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

    assert(app.hasPlugin<CounterPlugin>());
    assert(app.hasPlugin<DependentPlugin>());

    app.run();
    // frame 1: 10 → 11 → 1011; frame 2: 1012 ≥ 12 → exit, then Last → 2012
    assert(app.resource<Counter>().value == 2012);
}

void testCannotAddSystemsWhileRunning() {
    App app;
    bool threw = false;
    app.addSystem(Stage::Startup, [&app, &threw] {
        try { app.addSystem(Stage::Update, [] {}); } catch (const std::logic_error&) { threw = true; }
    });
    app.addSystem(Stage::First, [](AppControl& c) { c.requestExit(); });
    app.run();
    assert(threw);
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
    assert(g_log == expected);
}

void testShutdownRunsInReverseOrder() {
    std::vector<std::string> order;
    App app;
    app.addSystem(Stage::Shutdown, [&order] { order.push_back("first registered"); })
       .addSystem(Stage::Shutdown, [&order] { order.push_back("second registered"); })
       .addSystem(Stage::First, [](AppControl& c) { c.requestExit(); });
    app.run();
    const std::vector<std::string> expected { "second registered", "first registered" };
    assert(order == expected);
}

}

int main() {
    testWorldResources();
    testDestructionOrder();
    testSystemsAndStages();
    testStartupCanInsertResources();
    testMissingResourceIsReported();
    testPlugins();
    testCannotAddSystemsWhileRunning();
    testAppTeardownOrder();
    testShutdownRunsInReverseOrder();

    std::cout << "All app tests passed" << std::endl;
    return 0;
}
