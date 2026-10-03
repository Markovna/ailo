#pragma once

#include "PhysicsComponents.h"
#include "PhysicsWorld.h"

namespace ailo {

class App;

// Requires:    nothing (FixedTime is always present; PlatformPlugin feeds it the frame delta).
// Resources:   PhysicsWorld, PhysicsBodyHooks (removes Jolt bodies when PhysicsBody / RigidBody / Collider go away,
//              and rebuilds them when RigidBody / Collider are patched or replaced).
// FixedUpdate: physics::fixedUpdate, one system that, in this order,
//              creates bodies for new RigidBody + Collider + TransformComponent entities (adds PhysicsBody),
//              pushes kinematic targets and teleports (transforms written by gameplay) to Jolt,
//              PhysicsWorld::step(FixedTime::step), then reads back the poses of dynamic bodies.
//              FixedUpdate systems registered after this plugin see the post-step state.
// PostUpdate:  writes the pose of dynamic bodies, interpolated by FixedTime::alpha, into their world transform
//              (hierarchy::setWorld, so children follow).
//
// Dynamic bodies own their transform: put them at the root, or under parents that do not move. A world transform
// that changes for any other reason (e.g. a moving parent) is treated as a teleport.
struct PhysicsPlugin {
    PhysicsSettings settings;

    void build(App& app);
};

}
