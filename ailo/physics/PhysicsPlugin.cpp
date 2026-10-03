#include "PhysicsPlugin.h"

#include "app/App.h"

namespace ailo {

namespace {

void stepPhysics(PhysicsWorld& physics, const FixedTime& fixed) {
    physics.step(fixed.step);
}

}

void PhysicsPlugin::build(App& app) {
    app.insertResource<PhysicsWorld>(settings);

    app.addSystem(Stage::FixedUpdate, stepPhysics, "PhysicsPlugin::step");
}

}
