#include "PhysicsWorld.h"

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <format>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

#include "PhysicsMath.h"

namespace ailo {

namespace {

std::mutex g_runtimeMutex;
int g_runtimeRefs = 0;

void trace(const char* fmt, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    std::cerr << "[Jolt] " << buffer << std::endl;
}

#ifdef JPH_ENABLE_ASSERTS
bool assertFailed(const char* expr, const char* msg, const char* file, JPH::uint line) {
    std::cerr << std::format("[Jolt] assert failed: {}:{}: ({}) {}", file, line, expr, msg ? msg : "") << std::endl;
    return true;   // break into the debugger
}
#endif

const char* describe(JPH::EPhysicsUpdateError error) {
    using enum JPH::EPhysicsUpdateError;
    if ((error & ManifoldCacheFull) != None) return "manifold cache full (raise maxContactConstraints)";
    if ((error & BodyPairCacheFull) != None) return "body pair cache full (raise maxBodyPairs)";
    if ((error & ContactConstraintsFull) != None) return "contact constraints full (raise maxContactConstraints)";
    return "unknown error";
}

}

PhysicsWorld::JoltRuntime::JoltRuntime() {
    std::scoped_lock lock(g_runtimeMutex);
    if (g_runtimeRefs++ > 0) {
        return;
    }

    JPH::RegisterDefaultAllocator();
    JPH::Trace = trace;
    JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = assertFailed;)

    if (!JPH::VerifyJoltVersionID()) {
        g_runtimeRefs--;
        throw std::runtime_error("Jolt was built with different config defines than ailo (see CMakeLists.txt)");
    }

    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();
}

PhysicsWorld::JoltRuntime::~JoltRuntime() {
    std::scoped_lock lock(g_runtimeMutex);
    if (--g_runtimeRefs > 0) {
        return;
    }

    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;
}

PhysicsWorld::PhysicsWorld(const PhysicsSettings& settings) : m_settings(settings) {
    m_tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(settings.tempAllocatorSize);

    const int hardwareThreads = static_cast<int>(std::thread::hardware_concurrency());
    const int workers = settings.workerThreads >= 0 ? settings.workerThreads : std::max(1, hardwareThreads - 1);
    m_jobSystem = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, workers);

    m_system = std::make_unique<JPH::PhysicsSystem>();
    m_system->Init(
        settings.maxBodies,
        0,   // number of body mutexes: 0 picks Jolt's default
        settings.maxBodyPairs,
        settings.maxContactConstraints,
        m_broadPhaseLayers,
        m_objectVsBroadPhaseFilter,
        m_objectLayerPairFilter);
    m_system->SetGravity(physics::toJolt(settings.gravity));
}

PhysicsWorld::~PhysicsWorld() = default;

void PhysicsWorld::step(float dt) {
    const auto start = std::chrono::steady_clock::now();

    const JPH::EPhysicsUpdateError error =
        m_system->Update(dt, m_settings.collisionSteps, m_tempAllocator.get(), m_jobSystem.get());
    if (error != JPH::EPhysicsUpdateError::None) {
        // The step still completes, but some contacts were dropped: objects may sink into each other.
        std::cerr << "[Physics] step: " << describe(error) << std::endl;
    }

    m_stats.stepMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
    m_stats.numBodies = m_system->GetNumBodies();
    m_stats.numActiveBodies = m_system->GetNumActiveBodies(JPH::EBodyType::RigidBody);
}

void PhysicsWorld::optimizeBroadPhase() {
    m_system->OptimizeBroadPhase();
}

JPH::BodyInterface& PhysicsWorld::bodies() {
    return m_system->GetBodyInterface();
}

JPH::BodyInterface& PhysicsWorld::bodiesNoLock() {
    return m_system->GetBodyInterfaceNoLock();
}

glm::vec3 PhysicsWorld::gravity() const {
    return physics::toGlm(m_system->GetGravity());
}

void PhysicsWorld::setGravity(const glm::vec3& gravity) {
    m_system->SetGravity(physics::toJolt(gravity));
}

}
