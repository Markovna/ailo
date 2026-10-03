#include "PhysicsBodies.h"
#include "PhysicsMath.h"
#include "PhysicsPlugin.h"
#include "PhysicsWorld.h"

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <optional>

#include "app/App.h"
#include "ecs/Hierarchy.h"

using namespace ailo;

namespace {

constexpr float kStep = 1.0f / 60.0f;

bool near(float a, float b, float epsilon = 1e-5f) { return std::abs(a - b) <= epsilon; }

bool near(const glm::vec3& a, const glm::vec3& b, float epsilon = 1e-5f) {
    return near(a.x, b.x, epsilon) && near(a.y, b.y, epsilon) && near(a.z, b.z, epsilon);
}

// 200 x 2 x 200 static box whose top face is at y = 0.
JPH::BodyID addFloor(JPH::BodyInterface& bodies) {
    JPH::BodyCreationSettings settings(
        new JPH::BoxShape(JPH::Vec3(100.0f, 1.0f, 100.0f)),
        JPH::RVec3(0.0f, -1.0f, 0.0f), JPH::Quat::sIdentity(),
        JPH::EMotionType::Static, ObjectLayers::NonMoving);
    return bodies.CreateAndAddBody(settings, JPH::EActivation::DontActivate);
}

JPH::BodyID addSphere(JPH::BodyInterface& bodies, const glm::vec3& position, float radius, bool damped = true) {
    JPH::BodyCreationSettings settings(
        new JPH::SphereShape(radius),
        physics::toJoltR(position), JPH::Quat::sIdentity(),
        JPH::EMotionType::Dynamic, ObjectLayers::Moving);
    if (!damped) {
        settings.mLinearDamping = 0.0f;
    }
    return bodies.CreateAndAddBody(settings, JPH::EActivation::Activate);
}

glm::vec3 position(JPH::BodyInterface& bodies, JPH::BodyID id) {
    return physics::toGlm(bodies.GetCenterOfMassPosition(id));
}

void testConversions() {
    const glm::vec3 v(1.0f, -2.0f, 3.5f);
    assert(near(physics::toGlm(physics::toJolt(v)), v));
    assert(near(physics::toGlm(physics::toJoltR(v)), v));

    // Component order differs: glm::quat is (w, x, y, z), JPH::Quat is (x, y, z, w).
    const glm::quat q = glm::angleAxis(0.7f, glm::normalize(glm::vec3(1.0f, 2.0f, -0.5f)));
    const JPH::Quat jq = physics::toJolt(q);
    assert(near(jq.GetX(), q.x) && near(jq.GetY(), q.y) && near(jq.GetZ(), q.z) && near(jq.GetW(), q.w));

    // Both libraries must rotate a vector the same way.
    assert(near(physics::toGlm(jq * physics::toJolt(v)), q * v, 1e-4f));

    const glm::quat back = physics::toGlm(jq);
    assert(near(back.w, q.w) && near(back.x, q.x) && near(back.y, q.y) && near(back.z, q.z));
}

void testSphereRestsOnFloor() {
    constexpr float kRadius = 0.5f;

    PhysicsWorld world;
    auto& bodies = world.bodiesNoLock();

    const JPH::BodyID floor = addFloor(bodies);
    const JPH::BodyID sphere = addSphere(bodies, { 0.0f, 5.0f, 0.0f }, kRadius);
    bodies.SetLinearVelocity(sphere, JPH::Vec3(0.0f, -5.0f, 0.0f));
    world.optimizeBroadPhase();

    float lowestY = 5.0f;
    int steps = 0;
    for (; steps < 600 && bodies.IsActive(sphere); steps++) {
        world.step(kStep);
        lowestY = std::min(lowestY, position(bodies, sphere).y);
    }

    const glm::vec3 rest = position(bodies, sphere);
    std::cout << "sphere at rest after " << steps << " steps, y = " << rest.y << std::endl;

    assert(!bodies.IsActive(sphere) && "sphere never went to sleep");
    // Resting contacts are allowed to sink up to the penetration slop.
    const float slop = world.system().GetPhysicsSettings().mPenetrationSlop;
    assert(near(rest.y, kRadius, slop + 1e-3f));
    assert(near(rest.x, 0.0f, 1e-3f) && near(rest.z, 0.0f, 1e-3f));
    assert(lowestY > 0.0f && "sphere tunneled into the floor");

    assert(world.stats().numBodies == 2);
    assert(world.stats().numActiveBodies == 0);

    bodies.RemoveBody(sphere);
    bodies.DestroyBody(sphere);
    bodies.RemoveBody(floor);
    bodies.DestroyBody(floor);
    assert(world.system().GetNumBodies() == 0);
}

// A world that simulates a falling sphere for a few steps: needs the registered collision dispatch.
void simulateCollision(PhysicsWorld& world) {
    auto& bodies = world.bodiesNoLock();
    const JPH::BodyID floor = addFloor(bodies);
    const JPH::BodyID sphere = addSphere(bodies, { 0.0f, 0.4f, 0.0f }, 0.5f);   // starts overlapping the floor
    for (int i = 0; i < 30; i++) {
        world.step(kStep);
    }
    assert(position(bodies, sphere).y > 0.4f && "contact did not push the sphere out");
    bodies.RemoveBody(sphere);
    bodies.DestroyBody(sphere);
    bodies.RemoveBody(floor);
    bodies.DestroyBody(floor);
}

void testRuntimeIsRefCounted() {
    {
        std::optional<PhysicsWorld> first(std::in_place);
        PhysicsWorld second;
        first.reset();               // must not unregister Jolt's types while `second` is alive
        simulateCollision(second);
    }
    PhysicsWorld again;              // global state is set up again after the last world is gone
    simulateCollision(again);
}

void testGravity() {
    PhysicsWorld world(PhysicsSettings { .gravity = { 0.0f, 0.0f, 0.0f } });
    assert(near(world.gravity(), glm::vec3(0.0f)));

    auto& bodies = world.bodiesNoLock();
    const JPH::BodyID sphere = addSphere(bodies, { 0.0f, 10.0f, 0.0f }, 0.5f);
    for (int i = 0; i < 10; i++) {
        world.step(kStep);
    }
    assert(near(position(bodies, sphere), { 0.0f, 10.0f, 0.0f }));

    world.setGravity({ 2.0f, 0.0f, 0.0f });
    assert(near(world.gravity(), { 2.0f, 0.0f, 0.0f }));
    world.step(kStep);
    assert(position(bodies, sphere).x > 0.0f);

    bodies.RemoveBody(sphere);
    bodies.DestroyBody(sphere);
}

// The plugin steps once per FixedUpdate, with FixedTime::step as dt.
void testPluginStepsOncePerFixedTick() {
    App app;
    app.addPlugin(PhysicsPlugin {});

    JPH::BodyID sphere;
    int fixedTicks = 0;
    int frame = 0;
    glm::vec3 velocity {};

    app.addSystem(Stage::Startup, [&](PhysicsWorld& world, FixedTime& fixed) {
           fixed.step = kStep;
           sphere = addSphere(world.bodiesNoLock(), { 0.0f, 100.0f, 0.0f }, 0.5f, false);
       })
       .addSystem(Stage::First, [](FixedTime& fixed) { fixed.accumulate(2.25f * kStep); })
       .addSystem(Stage::FixedUpdate, [&] { fixedTicks++; })
       .addSystem(Stage::Last, [&](PhysicsWorld& world, AppControl& control) {
           velocity = physics::toGlm(world.bodiesNoLock().GetLinearVelocity(sphere));
           if (++frame == 2) control.requestExit();
       });
    app.run();

    // 2 frames x 2.25 steps: 2 steps per frame, 0.5 steps left over.
    // (Not 2.5: the second frame would end exactly on a step boundary and float rounding decides the count.)
    assert(fixedTicks == 4);
    const float expected = -9.81f * 4.0f * kStep;   // undamped free fall: v = g * t
    assert(near(velocity.y, expected, 1e-3f));

    app.resource<PhysicsWorld>().bodiesNoLock().RemoveBody(sphere);
    app.resource<PhysicsWorld>().bodiesNoLock().DestroyBody(sphere);
}

// ---- ECS sync -------------------------------------------------------------------------------------------------

// A registry wired to a PhysicsWorld, stepped like PhysicsPlugin does.
struct Fixture {
    PhysicsWorld world;
    entt::registry reg;
    PhysicsBodyHooks hooks { reg, world };

    // Bodies are destroyed through the hooks, which must still be connected.
    ~Fixture() { reg.clear(); }

    entt::entity spawn(const Transform& pose, const RigidBody& rb, const Collider& collider) {
        const entt::entity e = reg.create();
        reg.emplace<TransformComponent>(e, pose);
        reg.emplace<RigidBody>(e, rb);
        reg.emplace<Collider>(e, collider);
        return e;
    }

    // 100 x 1 x 100 static floor whose top face is at y = 0.
    entt::entity spawnFloor() {
        return spawn(Transform { .position = { 0.0f, -0.5f, 0.0f } },
                     RigidBody { .motionType = MotionType::Static },
                     Collider { .shape = BoxShape { .halfExtents = { 50.0f, 0.5f, 50.0f } } });
    }

    void tick(int steps = 1) {
        for (int i = 0; i < steps; i++) {
            physics::fixedUpdate(reg, world, kStep);
        }
        physics::interpolate(reg, 1.0f);
    }

    // Steps until the body sleeps; returns false if it never does.
    bool settle(entt::entity e, int maxSteps = 600) {
        tick();
        for (int i = 0; i < maxSteps; i++) {
            if (!world.bodiesNoLock().IsActive(reg.get<PhysicsBody>(e).id)) return true;
            tick();
        }
        return false;
    }

    const Transform& pose(entt::entity e) { return reg.get<TransformComponent>(e).world(); }
    glm::vec3 bodyPosition(entt::entity e) { return position(world.bodiesNoLock(), reg.get<PhysicsBody>(e).id); }
    JPH::uint numBodies() const { return world.system().GetNumBodies(); }
    float slop() const { return world.system().GetPhysicsSettings().mPenetrationSlop; }
};

const RigidBody kFloating { .linearDamping = 0.0f, .angularDamping = 0.0f, .gravityFactor = 0.0f };

void testBodiesAreCreatedOnNextStep() {
    Fixture f;
    const entt::entity e = f.spawn({}, {}, {});
    assert(!f.reg.all_of<PhysicsBody>(e));

    f.tick();
    assert(f.reg.all_of<PhysicsBody>(e));
    const PhysicsBody& body = f.reg.get<PhysicsBody>(e);
    assert(!body.id.IsInvalid());
    assert(physics::entityOf(f.world, body.id) == e);
    assert(f.numBodies() == 1);

    // Entities missing a Collider are not bodies.
    const entt::entity noCollider = f.reg.create();
    f.reg.emplace<TransformComponent>(noCollider);
    f.reg.emplace<RigidBody>(noCollider);
    f.tick();
    assert(!f.reg.all_of<PhysicsBody>(noCollider));
    assert(f.numBodies() == 1);
}

void testBoxFallsAndRestsOnFloor() {
    Fixture f;
    f.spawnFloor();
    const entt::entity box = f.spawn(Transform { .position = { 0.0f, 3.0f, 0.0f } }, {},
                                     Collider { .shape = BoxShape { .halfExtents = glm::vec3(0.5f) } });
    assert(f.settle(box));

    // Written back to the ECS, resting on the floor.
    assert(near(f.pose(box).position.y, 0.5f, f.slop() + 1e-3f));
    assert(near(f.pose(box).position, f.bodyPosition(box), 1e-4f));
}

void testChildrenFollowBody() {
    Fixture f;
    f.spawnFloor();
    const entt::entity root = f.spawn(Transform { .position = { 0.0f, 2.0f, 0.0f } }, {},
                                      Collider { .shape = SphereShape { .radius = 0.5f } });
    const entt::entity child = f.reg.create();
    f.reg.emplace<TransformComponent>(child, Transform { .position = { 0.0f, 1.0f, 0.0f } });
    hierarchy::setParent(f.reg, child, root, hierarchy::Keep::Local);

    assert(f.settle(root));
    const Transform& r = f.pose(root);
    assert(r.position.y < 1.0f);
    assert(near(f.pose(child).position, r.position + r.rotation * glm::vec3(0.0f, 1.0f, 0.0f), 1e-4f));
}

void testRemovingComponentsRemovesBody() {
    Fixture f;
    const entt::entity a = f.spawn({}, kFloating, {});
    const entt::entity b = f.spawn({}, kFloating, {});
    const entt::entity c = f.spawn({}, kFloating, {});
    f.tick();
    assert(f.numBodies() == 3);

    f.reg.destroy(a);
    assert(f.numBodies() == 2);

    f.reg.remove<RigidBody>(b);
    assert(!f.reg.all_of<PhysicsBody>(b));
    assert(f.numBodies() == 1);
    f.tick();
    assert(!f.reg.all_of<PhysicsBody>(b));   // not recreated without a RigidBody

    f.reg.emplace<RigidBody>(b, kFloating);  // and back
    f.tick();
    assert(f.reg.all_of<PhysicsBody>(b));
    assert(f.numBodies() == 2);

    f.reg.remove<Collider>(c);
    assert(f.numBodies() == 1);

    f.reg.clear();
    assert(f.numBodies() == 0);
}

void testPatchRebuildsBody() {
    Fixture f;
    const entt::entity e = f.spawn(Transform { .position = { 0.0f, 10.0f, 0.0f } }, {}, {});
    f.tick(10);
    const float y = f.pose(e).position.y;
    assert(y < 10.0f);

    f.reg.patch<RigidBody>(e, [](RigidBody& rb) { rb.motionType = MotionType::Static; });
    assert(!f.reg.all_of<PhysicsBody>(e));
    f.tick(10);
    assert(f.reg.get<PhysicsBody>(e).motionType == MotionType::Static);
    assert(f.pose(e).position.y == y);   // static now: stays where it was
    assert(f.numBodies() == 1);

    f.reg.patch<Collider>(e, [](Collider& c) { c.shape = SphereShape { .radius = 2.0f }; });
    f.tick();
    assert(f.numBodies() == 1);
}

void testKinematicFollowsTransform() {
    Fixture f;
    const entt::entity e = f.spawn({}, RigidBody { .motionType = MotionType::Kinematic }, {});
    f.tick();

    hierarchy::setWorld(f.reg, e, Transform { .position = { 3.0f, 0.0f, 0.0f } });
    f.tick();
    assert(near(f.bodyPosition(e), { 3.0f, 0.0f, 0.0f }, 1e-4f));
    // It moved with a velocity (so it pushes dynamic bodies), and stops once the target stops changing.
    f.tick();
    const JPH::BodyID id = f.reg.get<PhysicsBody>(e).id;
    assert(f.world.bodiesNoLock().GetLinearVelocity(id).IsNearZero());
    // The ECS transform stays the source of truth.
    assert(f.pose(e).position == glm::vec3(3.0f, 0.0f, 0.0f));
}

void testKinematicPushesDynamic() {
    Fixture f;
    const entt::entity pusher = f.spawn(Transform { .position = { -2.0f, 0.0f, 0.0f } },
                                        RigidBody { .motionType = MotionType::Kinematic }, {});
    const entt::entity ball = f.spawn({}, kFloating, Collider { .shape = SphereShape { .radius = 0.5f } });
    f.tick();

    for (int i = 1; i <= 60; i++) {
        hierarchy::setWorld(f.reg, pusher, Transform { .position = { -2.0f + 0.05f * i, 0.0f, 0.0f } });
        f.tick();
    }
    assert(f.pose(ball).position.x > 1.0f);
}

void testTeleport() {
    Fixture f;
    const entt::entity e = f.spawn({}, kFloating, {});
    f.tick();

    hierarchy::setWorld(f.reg, e, Transform { .position = { 5.0f, 5.0f, 5.0f } });
    f.tick();
    assert(near(f.bodyPosition(e), { 5.0f, 5.0f, 5.0f }, 1e-4f));
    assert(near(f.pose(e).position, { 5.0f, 5.0f, 5.0f }, 1e-4f));

    // Static bodies move too (rarely needed: it is expensive for the broad phase).
    const entt::entity wall = f.spawn({}, RigidBody { .motionType = MotionType::Static }, {});
    f.tick();
    hierarchy::setWorld(f.reg, wall, Transform { .position = { 0.0f, 0.0f, 9.0f } });
    f.tick();
    assert(near(f.bodyPosition(wall), { 0.0f, 0.0f, 9.0f }, 1e-4f));
}

void testInterpolation() {
    Fixture f;
    RigidBody rb = kFloating;
    rb.linearVelocity = { 1.0f / kStep, 0.0f, 0.0f };   // 1 unit per step
    const entt::entity e = f.spawn({}, rb, Collider { .shape = SphereShape {} });

    f.tick();   // x: 0 -> 1
    physics::interpolate(f.reg, 0.25f);
    assert(near(f.pose(e).position.x, 0.25f, 1e-4f));
    physics::interpolate(f.reg, 0.75f);
    assert(near(f.pose(e).position.x, 0.75f, 1e-4f));

    // Writing the interpolated pose is not mistaken for a teleport.
    f.tick();   // x: 1 -> 2
    assert(near(f.pose(e).position.x, 2.0f, 1e-4f));
    physics::interpolate(f.reg, 0.5f);
    assert(near(f.pose(e).position.x, 1.5f, 1e-4f));
}

void testScale() {
    Fixture f;
    f.spawnFloor();
    const entt::entity e = f.spawn(Transform { .position = { 0.0f, 3.0f, 0.0f }, .scale = glm::vec3(2.0f) }, {},
                                   Collider { .shape = SphereShape { .radius = 0.5f } });
    assert(f.settle(e));
    assert(near(f.pose(e).position.y, 1.0f, f.slop() + 1e-3f));   // radius 0.5 at scale 2

    // A changed scale rebuilds the shape.
    Transform bigger = f.pose(e);
    bigger.scale = glm::vec3(4.0f);
    bigger.position.y = 3.0f;
    hierarchy::setWorld(f.reg, e, bigger);
    assert(f.settle(e));
    assert(near(f.pose(e).position.y, 2.0f, f.slop() + 1e-3f));

    // Offsets are scaled with the shape: at scale 2 a sphere 1 unit above the origin sits 2 units above it,
    // so resting on the floor (radius 1) puts the origin at y = 1 - 2 = -1.
    const entt::entity offset = f.spawn(Transform { .position = { 10.0f, 5.0f, 0.0f }, .scale = glm::vec3(2.0f) }, {},
                                        Collider { .shape = SphereShape { .radius = 0.5f }, .offset = { 0.0f, 1.0f, 0.0f } });
    assert(f.settle(offset));
    assert(near(f.pose(offset).position.y, -1.0f, f.slop() + 1e-3f));
}

void testInvalidColliderIsSkipped() {
    Fixture f;
    const entt::entity flat = f.spawn({}, {}, Collider { .shape = BoxShape { .halfExtents = { 1.0f, 0.0f, 1.0f } } });
    const entt::entity point = f.spawn({}, {}, Collider { .shape = SphereShape { .radius = 0.0f } });
    f.tick(3);

    // A PhysicsBody without a body, so the error is reported once instead of every step.
    assert(f.reg.get<PhysicsBody>(flat).id.IsInvalid());
    assert(f.reg.get<PhysicsBody>(point).id.IsInvalid());
    assert(f.numBodies() == 0);

    // Fixing the collider retries.
    f.reg.patch<Collider>(flat, [](Collider& c) { c.shape = BoxShape { .halfExtents = glm::vec3(1.0f) }; });
    f.tick();
    assert(!f.reg.get<PhysicsBody>(flat).id.IsInvalid());
    f.reg.destroy(point);   // destroying a body-less PhysicsBody is fine
    assert(f.numBodies() == 1);
}

void testMassOverride() {
    Fixture f;
    const entt::entity heavy = f.spawn({}, RigidBody { .mass = 50.0f }, {});
    const entt::entity defaultMass = f.spawn({}, {}, Collider { .shape = BoxShape { .halfExtents = glm::vec3(0.5f) } });
    f.tick();

    auto inverseMass = [&](entt::entity entity) {
        JPH::BodyLockRead lock(f.world.system().GetBodyLockInterfaceNoLock(), f.reg.get<PhysicsBody>(entity).id);
        return lock.GetBody().GetMotionProperties()->GetInverseMass();
    };
    assert(near(inverseMass(heavy), 1.0f / 50.0f));
    assert(near(inverseMass(defaultMass), 1.0f / 1000.0f, 1e-6f));   // 1 m^3 at the default density
}

// Through the real plugin: bodies are created, simulated and written back as the App runs.
void testPluginSimulatesEntities() {
    App app;
    app.addPlugin(PhysicsPlugin {});

    entt::entity box = entt::null;
    int frames = 0;
    app.addSystem(Stage::Startup, [&](World& world) {
           Scene& scene = world.scene();
           const entt::entity floor = scene.addEntity();
           scene.addComponent<TransformComponent>(floor, Transform { .position = { 0.0f, -0.5f, 0.0f } });
           scene.addComponent<RigidBody>(floor, RigidBody { .motionType = MotionType::Static });
           scene.addComponent<Collider>(floor, Collider { .shape = BoxShape { .halfExtents = { 50.0f, 0.5f, 50.0f } } });

           box = scene.addEntity();
           scene.addComponent<TransformComponent>(box, Transform { .position = { 0.0f, 3.0f, 0.0f } });
           scene.addComponent<RigidBody>(box);
           scene.addComponent<Collider>(box);
       })
       .addSystem(Stage::First, [](FixedTime& fixed) { fixed.accumulate(kStep * 1.5f); })
       .addSystem(Stage::Last, [&](World& world, PhysicsWorld& physics, AppControl& control) {
           const auto* body = world.scene().tryGet<PhysicsBody>(box);
           const bool asleep = body && !physics.bodiesNoLock().IsActive(body->id);
           if (asleep || ++frames == 1000) control.requestExit();
       });
    app.run();

    assert(frames < 1000 && "box never came to rest");
    const float y = app.world().scene().get<TransformComponent>(box).world().position.y;
    const float slop = app.resource<PhysicsWorld>().system().GetPhysicsSettings().mPenetrationSlop;
    assert(near(y, 0.5f, slop + 1e-3f));
    // The App tears down with live bodies: the hooks remove them while clearing the scene.
}

}

int main() {
    testConversions();
    testSphereRestsOnFloor();
    testRuntimeIsRefCounted();
    testGravity();
    testPluginStepsOncePerFixedTick();

    testBodiesAreCreatedOnNextStep();
    testBoxFallsAndRestsOnFloor();
    testChildrenFollowBody();
    testRemovingComponentsRemovesBody();
    testPatchRebuildsBody();
    testKinematicFollowsTransform();
    testKinematicPushesDynamic();
    testTeleport();
    testInterpolation();
    testScale();
    testInvalidColliderIsSkipped();
    testMassOverride();
    testPluginSimulatesEntities();

    std::cout << "physicstest: all passed\n";
    return 0;
}
