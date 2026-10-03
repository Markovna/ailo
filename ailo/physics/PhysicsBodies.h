#pragma once

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>

#include <entt/entity/registry.hpp>

#include "PhysicsComponents.h"

namespace ailo {

class PhysicsWorld;

// Keeps Jolt bodies in step with the registry. Resource inserted by PhysicsPlugin, after PhysicsWorld.
//   on_destroy<PhysicsBody>               → removes and destroys the Jolt body
//   on_destroy/on_update<RigidBody|Collider> → removes PhysicsBody, so the body is rebuilt (or gone for good)
// Disconnects on destruction, i.e. before PhysicsWorld is destroyed.
class PhysicsBodyHooks {
public:
    PhysicsBodyHooks(entt::registry& registry, PhysicsWorld& world);
    ~PhysicsBodyHooks();

    PhysicsBodyHooks(const PhysicsBodyHooks&) = delete;
    PhysicsBodyHooks& operator=(const PhysicsBodyHooks&) = delete;

private:
    void destroyBody(entt::registry& registry, entt::entity entity);
    void invalidate(entt::registry& registry, entt::entity entity);

    entt::registry& m_registry;
    PhysicsWorld& m_world;
};

namespace physics {

// Builds the Jolt shape of a collider at the given world scale, or nullptr (and logs why) if it is invalid.
JPH::ShapeRefC buildShape(const Collider& collider, const glm::vec3& scale);

// The entity a body was created for.
entt::entity entityOf(PhysicsWorld& world, JPH::BodyID id);

// One fixed step of the simulation, as PhysicsPlugin runs it in FixedUpdate. The steps below always run together,
// in this order: createPendingBodies, pushTransforms, PhysicsWorld::step(dt), pullTransforms.
void fixedUpdate(entt::registry& registry, PhysicsWorld& world, float dt);

// Creates bodies for entities with RigidBody + Collider + TransformComponent but no PhysicsBody yet.
// An entity whose shape is invalid gets a PhysicsBody without a body, so it is not retried every step.
void createPendingBodies(entt::registry& registry, PhysicsWorld& world);

// ECS → Jolt: kinematic bodies move to their transform over `step` seconds; static and dynamic bodies whose
// transform gameplay changed are teleported. Scale changes rebuild the shape.
void pushTransforms(entt::registry& registry, PhysicsWorld& world, float step);

// Jolt → PhysicsBody: shifts the simulated pose of dynamic bodies (current → previous, body → current).
void pullTransforms(entt::registry& registry, PhysicsWorld& world);

// PhysicsBody → ECS: writes previous..current at `alpha` into the world transform of dynamic bodies.
// Runs in PostUpdate, separately from fixedUpdate: once per frame, however many fixed steps the frame had.
void interpolate(entt::registry& registry, float alpha);

}

}
