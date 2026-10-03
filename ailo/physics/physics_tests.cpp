// Phase 1 smoke test: Jolt builds, links and simulates with our compiler settings.
// A sphere is dropped onto a static box and must come to rest on top of it.

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <iostream>
#include <thread>

namespace {

namespace Layers {
constexpr JPH::ObjectLayer NON_MOVING = 0;
constexpr JPH::ObjectLayer MOVING = 1;
constexpr JPH::ObjectLayer NUM_LAYERS = 2;
}

namespace BroadPhaseLayers {
constexpr JPH::BroadPhaseLayer NON_MOVING(0);
constexpr JPH::BroadPhaseLayer MOVING(1);
constexpr JPH::uint NUM_LAYERS = 2;
}

class BroadPhaseLayerMap final : public JPH::BroadPhaseLayerInterface {
public:
    JPH::uint GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::NUM_LAYERS; }

    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        return layer == Layers::NON_MOVING ? BroadPhaseLayers::NON_MOVING : BroadPhaseLayers::MOVING;
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
        return layer == BroadPhaseLayers::NON_MOVING ? "NON_MOVING" : "MOVING";
    }
#endif
};

class ObjectVsBroadPhaseFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer bpLayer) const override {
        // Static objects only need to be tested against moving ones.
        return layer == Layers::MOVING || bpLayer == BroadPhaseLayers::MOVING;
    }
};

class ObjectPairFilter final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        return a == Layers::MOVING || b == Layers::MOVING;
    }
};

void trace(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    std::vfprintf(stderr, fmt, args);
    va_end(args);
    std::fputc('\n', stderr);
}

#ifdef JPH_ENABLE_ASSERTS
bool assertFailed(const char* expr, const char* msg, const char* file, JPH::uint line) {
    std::fprintf(stderr, "Jolt assert: %s:%u: (%s) %s\n", file, line, expr, msg ? msg : "");
    return true;   // break into the debugger
}
#endif

void testSphereRestsOnFloor() {
    constexpr float kFloorTop = 0.0f;
    constexpr float kRadius = 0.5f;
    constexpr float kStep = 1.0f / 60.0f;

    JPH::TempAllocatorImpl tempAllocator(10 * 1024 * 1024);
    const int workers = std::max(1, static_cast<int>(std::thread::hardware_concurrency()) - 1);
    JPH::JobSystemThreadPool jobSystem(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, workers);

    BroadPhaseLayerMap broadPhaseLayers;
    ObjectVsBroadPhaseFilter objectVsBroadPhase;
    ObjectPairFilter objectPairs;

    JPH::PhysicsSystem physics;
    physics.Init(1024, 0, 1024, 1024, broadPhaseLayers, objectVsBroadPhase, objectPairs);

    JPH::BodyInterface& bodies = physics.GetBodyInterface();

    // 200 x 2 x 200 floor whose top face is at y = 0.
    JPH::BodyCreationSettings floorSettings(
        new JPH::BoxShape(JPH::Vec3(100.0f, 1.0f, 100.0f)),
        JPH::RVec3(0.0f, kFloorTop - 1.0f, 0.0f), JPH::Quat::sIdentity(),
        JPH::EMotionType::Static, Layers::NON_MOVING);
    const JPH::BodyID floor = bodies.CreateAndAddBody(floorSettings, JPH::EActivation::DontActivate);

    JPH::BodyCreationSettings sphereSettings(
        new JPH::SphereShape(kRadius),
        JPH::RVec3(0.0f, 5.0f, 0.0f), JPH::Quat::sIdentity(),
        JPH::EMotionType::Dynamic, Layers::MOVING);
    const JPH::BodyID sphere = bodies.CreateAndAddBody(sphereSettings, JPH::EActivation::Activate);
    bodies.SetLinearVelocity(sphere, JPH::Vec3(0.0f, -5.0f, 0.0f));

    physics.OptimizeBroadPhase();

    // The sphere must fall, then go to sleep once it has settled.
    float lowestY = 5.0f;
    int steps = 0;
    for (; steps < 600 && bodies.IsActive(sphere); steps++) {
        const JPH::EPhysicsUpdateError error = physics.Update(kStep, 1, &tempAllocator, &jobSystem);
        assert(error == JPH::EPhysicsUpdateError::None);
        lowestY = std::min(lowestY, static_cast<float>(bodies.GetCenterOfMassPosition(sphere).GetY()));
    }

    const JPH::RVec3 rest = bodies.GetCenterOfMassPosition(sphere);
    std::cout << "sphere at rest after " << steps << " steps, y = " << rest.GetY() << std::endl;

    assert(!bodies.IsActive(sphere) && "sphere never went to sleep");
    // Resting contacts are allowed to sink up to the penetration slop.
    const float slop = physics.GetPhysicsSettings().mPenetrationSlop;
    assert(std::abs(static_cast<float>(rest.GetY()) - (kFloorTop + kRadius)) <= slop + 1e-3f);
    assert(std::abs(static_cast<float>(rest.GetX())) < 1e-3f && std::abs(static_cast<float>(rest.GetZ())) < 1e-3f);
    assert(lowestY > kFloorTop && "sphere tunneled into the floor");

    bodies.RemoveBody(sphere);
    bodies.DestroyBody(sphere);
    bodies.RemoveBody(floor);
    bodies.DestroyBody(floor);
    assert(physics.GetNumBodies() == 0);
}

}

int main() {
    JPH::RegisterDefaultAllocator();
    JPH::Trace = trace;
    JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = assertFailed;)

    // Fails if the defines our code sees differ from the ones Jolt was built with.
    assert(JPH::VerifyJoltVersionID());

    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();

    testSphereRestsOnFloor();

    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;

    std::cout << "physicstest: all passed\n";
    return 0;
}
