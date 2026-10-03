#include "PhysicsPlugin.h"

#include "PhysicsBodies.h"
#include "app/App.h"

namespace ailo {

namespace {

void fixedUpdate(World& world, PhysicsWorld& physics, const FixedTime& fixed) {
    physics::fixedUpdate(world.scene().registry(), physics, fixed.step);
}

void interpolate(World& world, const FixedTime& fixed) {
    physics::interpolate(world.scene().registry(), fixed.alpha);
}

}

void PhysicsPlugin::build(App& app) {
    auto& physics = app.insertResource<PhysicsWorld>(settings);
    app.insertResource<PhysicsBodyHooks>(app.world().scene().registry(), physics);

    app.addSystem(Stage::FixedUpdate, fixedUpdate, "PhysicsPlugin::fixedUpdate");
    app.addSystem(Stage::PostUpdate, interpolate, "PhysicsPlugin::interpolate");
}

}
