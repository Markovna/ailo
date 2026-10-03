#pragma once

#include "PhysicsWorld.h"

namespace ailo {

class App;

// Requires:    nothing (FixedTime is always present; PlatformPlugin feeds it the frame delta).
// Resources:   PhysicsWorld.
// FixedUpdate: PhysicsWorld::step(FixedTime::step). FixedUpdate systems registered after this plugin
//              see the post-step state.
// Teardown:    bodies still in the world are destroyed with the PhysicsWorld resource.
struct PhysicsPlugin {
    PhysicsSettings settings;

    void build(App& app);
};

}
