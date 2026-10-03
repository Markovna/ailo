#include "PhysicsBodies.h"

#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>

#include <algorithm>
#include <iostream>
#include <vector>

#include "PhysicsMath.h"
#include "PhysicsWorld.h"
#include "ecs/Hierarchy.h"

namespace ailo {

namespace {

template<typename... Fs>
struct overloaded : Fs... { using Fs::operator()...; };

JPH::EMotionType toJolt(MotionType type) {
    switch (type) {
        case MotionType::Static: return JPH::EMotionType::Static;
        case MotionType::Kinematic: return JPH::EMotionType::Kinematic;
        case MotionType::Dynamic: return JPH::EMotionType::Dynamic;
    }
    return JPH::EMotionType::Static;
}

JPH::ObjectLayer layerFor(MotionType type) {
    return type == MotionType::Static ? ObjectLayers::NonMoving : ObjectLayers::Moving;
}

bool samePose(const Transform& a, const Transform& b) {
    return a.position == b.position && a.rotation == b.rotation;
}

// Jolt requires unit quaternions; accumulated hierarchy rotations drift slightly.
JPH::Quat toJoltRotation(const glm::quat& q) {
    return physics::toJolt(glm::normalize(q));
}

JPH::ShapeRefC unwrap(const JPH::ShapeSettings::ShapeResult& result, const char* what) {
    if (result.HasError()) {
        std::cerr << "[Physics] invalid " << what << ": " << result.GetError() << std::endl;
        return nullptr;
    }
    return result.Get();
}

}

PhysicsBodyHooks::PhysicsBodyHooks(entt::registry& registry, PhysicsWorld& world)
    : m_registry(registry), m_world(world) {
    m_registry.on_destroy<PhysicsBody>().connect<&PhysicsBodyHooks::destroyBody>(*this);
    m_registry.on_destroy<RigidBody>().connect<&PhysicsBodyHooks::invalidate>(*this);
    m_registry.on_destroy<Collider>().connect<&PhysicsBodyHooks::invalidate>(*this);
    m_registry.on_update<RigidBody>().connect<&PhysicsBodyHooks::invalidate>(*this);
    m_registry.on_update<Collider>().connect<&PhysicsBodyHooks::invalidate>(*this);
}

PhysicsBodyHooks::~PhysicsBodyHooks() {
    m_registry.on_destroy<PhysicsBody>().disconnect(this);
    m_registry.on_destroy<RigidBody>().disconnect(this);
    m_registry.on_destroy<Collider>().disconnect(this);
    m_registry.on_update<RigidBody>().disconnect(this);
    m_registry.on_update<Collider>().disconnect(this);
}

void PhysicsBodyHooks::destroyBody(entt::registry& registry, entt::entity entity) {
    const JPH::BodyID id = registry.get<PhysicsBody>(entity).id;
    if (id.IsInvalid()) {
        return;
    }
    auto& bodies = m_world.bodiesNoLock();
    bodies.RemoveBody(id);
    bodies.DestroyBody(id);
}

void PhysicsBodyHooks::invalidate(entt::registry& registry, entt::entity entity) {
    registry.remove<PhysicsBody>(entity);
}

namespace physics {

JPH::ShapeRefC buildShape(const Collider& collider, const glm::vec3& scale) {
    JPH::ShapeRefC shape = std::visit(overloaded {
        [](const BoxShape& box) {
            // The convex radius rounds the corners and may not exceed the smallest half extent.
            const float smallest = std::min({ box.halfExtents.x, box.halfExtents.y, box.halfExtents.z });
            if (!(smallest > 0.0f)) {   // Jolt accepts a flat box with a zero convex radius; it has no volume
                std::cerr << "[Physics] invalid box collider: half extents must be positive" << std::endl;
                return JPH::ShapeRefC();
            }
            const float convexRadius = std::clamp(smallest, 0.0f, JPH::cDefaultConvexRadius);
            return unwrap(JPH::BoxShapeSettings(toJolt(box.halfExtents), convexRadius).Create(), "box collider");
        },
        [](const SphereShape& sphere) {
            return unwrap(JPH::SphereShapeSettings(sphere.radius).Create(), "sphere collider");
        },
        [](const CapsuleShape& capsule) {
            if (capsule.halfHeight <= 0.0f) {   // a capsule without a cylinder part is a sphere
                return unwrap(JPH::SphereShapeSettings(capsule.radius).Create(), "capsule collider");
            }
            return unwrap(JPH::CapsuleShapeSettings(capsule.halfHeight, capsule.radius).Create(), "capsule collider");
        },
    }, collider.shape);

    if (!shape) {
        return nullptr;
    }

    if (collider.offset != glm::vec3(0.0f) || collider.rotation != glm::identity<glm::quat>()) {
        shape = unwrap(JPH::RotatedTranslatedShapeSettings(
            toJolt(collider.offset), toJoltRotation(collider.rotation), shape).Create(), "collider offset");
        if (!shape) {
            return nullptr;
        }
    }

    if (scale != glm::vec3(1.0f)) {
        // Shapes that cannot scale non-uniformly (spheres, capsules) get the closest valid scale.
        shape = unwrap(shape->ScaleShape(toJolt(scale)), "collider scale");
    }

    return shape;
}

entt::entity entityOf(PhysicsWorld& world, JPH::BodyID id) {
    return static_cast<entt::entity>(static_cast<entt::id_type>(world.bodiesNoLock().GetUserData(id)));
}

void fixedUpdate(entt::registry& registry, PhysicsWorld& world, float dt) {
    createPendingBodies(registry, world);
    pushTransforms(registry, world, dt);
    world.step(dt);
    pullTransforms(registry, world);
}

void createPendingBodies(entt::registry& registry, PhysicsWorld& world) {
    // Collected first: adding PhysicsBody while iterating a view that excludes it would invalidate the view.
    auto pending = registry.view<RigidBody, Collider, TransformComponent>(entt::exclude<PhysicsBody>);
    const std::vector<entt::entity> entities(pending.begin(), pending.end());
    if (entities.empty()) {
        return;
    }

    auto& bodies = world.bodiesNoLock();
    for (const entt::entity entity : entities) {
        const RigidBody& rb = registry.get<RigidBody>(entity);
        const Transform& pose = registry.get<TransformComponent>(entity).world();

        PhysicsBody body {
            .motionType = rb.motionType,
            .shapeScale = pose.scale,
            .previous = pose,
            .current = pose,
            .synced = pose,
        };

        if (JPH::ShapeRefC shape = buildShape(registry.get<Collider>(entity), pose.scale)) {
            JPH::BodyCreationSettings settings(
                shape, toJoltR(pose.position), toJoltRotation(pose.rotation),
                ailo::toJolt(rb.motionType), layerFor(rb.motionType));
            settings.mUserData = static_cast<JPH::uint64>(entt::to_integral(entity));
            settings.mFriction = rb.friction;
            settings.mRestitution = rb.restitution;
            settings.mLinearDamping = rb.linearDamping;
            settings.mAngularDamping = rb.angularDamping;
            settings.mGravityFactor = rb.gravityFactor;
            settings.mAllowSleeping = rb.allowSleeping;
            if (rb.motionType != MotionType::Static) {
                settings.mLinearVelocity = toJolt(rb.linearVelocity);
                settings.mAngularVelocity = toJolt(rb.angularVelocity);
            }
            if (rb.motionType == MotionType::Dynamic && rb.mass > 0.0f) {
                settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
                settings.mMassPropertiesOverride.mMass = rb.mass;
            }

            const auto activation = rb.motionType == MotionType::Static
                ? JPH::EActivation::DontActivate : JPH::EActivation::Activate;
            body.id = bodies.CreateAndAddBody(settings, activation);
            if (body.id.IsInvalid()) {
                std::cerr << "[Physics] out of bodies (raise PhysicsSettings::maxBodies)" << std::endl;
            }
        }

        registry.emplace<PhysicsBody>(entity, body);
    }
}

void pushTransforms(entt::registry& registry, PhysicsWorld& world, float step) {
    auto& bodies = world.bodiesNoLock();
    for (auto [entity, body, tc] : registry.view<PhysicsBody, TransformComponent>().each()) {
        if (body.id.IsInvalid()) {
            continue;
        }
        const Transform& pose = tc.world();

        if (pose.scale != body.shapeScale) {
            body.shapeScale = pose.scale;
            if (JPH::ShapeRefC shape = buildShape(registry.get<Collider>(entity), pose.scale)) {
                bodies.SetShape(body.id, shape, true, JPH::EActivation::Activate);
            }
        }

        if (body.motionType == MotionType::Kinematic) {
            // MoveKinematic sets the velocity that reaches the target in one step. Once the target stops changing,
            // one more call with the same target zeroes that velocity, after which the body may sleep.
            if (!samePose(pose, body.synced) || bodies.IsActive(body.id)) {
                bodies.MoveKinematic(body.id, toJoltR(pose.position), toJoltRotation(pose.rotation), step);
            }
            body.synced = pose;
            continue;
        }

        if (!samePose(pose, body.synced)) {
            // Gameplay moved the entity: teleport. Velocities are kept.
            const auto activation = body.motionType == MotionType::Dynamic
                ? JPH::EActivation::Activate : JPH::EActivation::DontActivate;
            bodies.SetPositionAndRotation(body.id, toJoltR(pose.position), toJoltRotation(pose.rotation), activation);
            body.previous = body.current = body.synced = pose;
        }
    }
}

void pullTransforms(entt::registry& registry, PhysicsWorld& world) {
    auto& bodies = world.bodiesNoLock();
    for (auto [entity, body] : registry.view<PhysicsBody>().each()) {
        if (body.motionType != MotionType::Dynamic || body.id.IsInvalid()) {
            continue;
        }
        body.previous = body.current;
        if (bodies.IsActive(body.id)) {
            JPH::RVec3 position;
            JPH::Quat rotation;
            bodies.GetPositionAndRotation(body.id, position, rotation);
            body.current.position = toGlm(position);
            body.current.rotation = toGlm(rotation);
        }
    }
}

void interpolate(entt::registry& registry, float alpha) {
    for (auto [entity, body, tc] : registry.view<PhysicsBody, TransformComponent>().each()) {
        if (body.motionType != MotionType::Dynamic || body.id.IsInvalid()) {
            continue;
        }

        Transform pose = tc.world();
        pose.position = glm::mix(body.previous.position, body.current.position, alpha);
        pose.rotation = glm::slerp(body.previous.rotation, body.current.rotation, alpha);
        if (samePose(pose, tc.world())) {
            continue;   // asleep: skip propagating to the subtree
        }

        hierarchy::setWorld(registry, entity, pose);
        body.synced = tc.world();
    }
}

}

}
