#include "PhysicsMath.h"
#include "PhysicsPlugin.h"
#include "PhysicsWorld.h"

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <optional>

#include "app/App.h"

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

}

int main() {
    testConversions();
    testSphereRestsOnFloor();
    testRuntimeIsRefCounted();
    testGravity();
    testPluginStepsOncePerFixedTick();

    std::cout << "physicstest: all passed\n";
    return 0;
}
