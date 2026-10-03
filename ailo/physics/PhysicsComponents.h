#pragma once

#include <variant>

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Body/BodyID.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include "ecs/Transform.h"

namespace ailo {

enum class MotionType {
    Static,      // never moves; the ECS transform is read once (and again only when gameplay writes it)
    Kinematic,   // follows the ECS transform, pushing dynamic bodies out of the way
    Dynamic,     // simulated; the physics writes the ECS transform
};

// Makes an entity a physics body (together with Collider and TransformComponent).
// The body is created on the next FixedUpdate. Changing a field afterwards only takes effect when the change is
// signaled with registry.patch/replace, which rebuilds the body (velocities are lost).
struct RigidBody {
    MotionType motionType = MotionType::Dynamic;
    float mass = 0.0f;                    // kg; 0 = from the shape's volume at 1000 kg/m^3
    float friction = 0.2f;
    float restitution = 0.0f;             // bounciness, 0..1
    float linearDamping = 0.05f;
    float angularDamping = 0.05f;
    float gravityFactor = 1.0f;
    bool allowSleeping = true;
    glm::vec3 linearVelocity { 0.0f };    // initial velocities, applied when the body is created
    glm::vec3 angularVelocity { 0.0f };
};

struct BoxShape {
    glm::vec3 halfExtents { 0.5f };
};

struct SphereShape {
    float radius = 0.5f;
};

// Along the local Y axis: total height = 2 * (halfHeight + radius).
struct CapsuleShape {
    float halfHeight = 0.5f;              // of the cylinder part
    float radius = 0.5f;
};

// Collision shape of a RigidBody, in the entity's local space: it is scaled by the entity's world scale.
// Shapes are centered on the entity origin unless offset/rotation say otherwise.
struct Collider {
    std::variant<BoxShape, SphereShape, CapsuleShape> shape = BoxShape {};
    glm::vec3 offset { 0.0f };
    glm::quat rotation = glm::identity<glm::quat>();
};

// Added by PhysicsPlugin when it creates the body; removing it (or RigidBody, or Collider) removes the body.
// Every field is read-only for gameplay code.
struct PhysicsBody {
    JPH::BodyID id;
    MotionType motionType = MotionType::Static;
    glm::vec3 shapeScale { 1.0f };        // world scale the shape was built with

    // Simulated poses at the last two fixed steps, interpolated for rendering (dynamic bodies).
    Transform previous;
    Transform current;
    // World transform as last synced with the body. A different TransformComponent::world() means gameplay moved
    // the entity, and the body is teleported there.
    Transform synced;
};

}
