#pragma once

#include <cstdint>
#include <memory>

#include <glm/vec3.hpp>

#include "PhysicsLayers.h"

namespace JPH {
class BodyInterface;
class JobSystemThreadPool;
class PhysicsSystem;
class TempAllocatorImpl;
}

namespace ailo {

struct PhysicsSettings {
    uint32_t maxBodies = 65536;
    uint32_t maxBodyPairs = 65536;          // broad-phase pairs buffered per step
    uint32_t maxContactConstraints = 10240;
    uint32_t tempAllocatorSize = 10 * 1024 * 1024;
    int workerThreads = -1;                 // -1: hardware threads - 1 (the calling thread helps while it waits)
    int collisionSteps = 1;                 // sub-steps per step(); Jolt wants 1 per 1/60 s
    glm::vec3 gravity = { 0.0f, -9.81f, 0.0f };
};

struct PhysicsStats {
    float stepMs = 0.0f;                    // duration of the last step()
    uint32_t numBodies = 0;
    uint32_t numActiveBodies = 0;
};

// Owns the Jolt simulation: global Jolt state (allocator hooks, Factory, registered types), the job system,
// the temp allocator, the collision layer filters and the JPH::PhysicsSystem. Resource inserted by PhysicsPlugin.
// Jolt's global state is reference counted, so PhysicsWorlds may be created one after another (or side by side).
class PhysicsWorld {
public:
    explicit PhysicsWorld(const PhysicsSettings& settings = {});
    ~PhysicsWorld();

    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    // Advances the simulation by dt seconds. Must not run concurrently with other access to the bodies.
    void step(float dt);

    // Rebuilds the broad-phase trees. Call once after adding many bodies (e.g. after loading a level), not every frame.
    void optimizeBroadPhase();

    JPH::PhysicsSystem& system() { return *m_system; }
    const JPH::PhysicsSystem& system() const { return *m_system; }

    // Locking body interface, safe to use from any thread.
    JPH::BodyInterface& bodies();
    // Non-locking body interface: only from the main thread, outside step().
    JPH::BodyInterface& bodiesNoLock();

    glm::vec3 gravity() const;
    void setGravity(const glm::vec3& gravity);

    const PhysicsStats& stats() const { return m_stats; }

private:
    // Acquires Jolt's global state. Declared first so it is released after everything below.
    struct JoltRuntime {
        JoltRuntime();
        ~JoltRuntime();
        JoltRuntime(const JoltRuntime&) = delete;
        JoltRuntime& operator=(const JoltRuntime&) = delete;
    };

    JoltRuntime m_runtime;
    PhysicsSettings m_settings;
    PhysicsStats m_stats;

    // The PhysicsSystem keeps references to these, so they are declared (and outlive it) first.
    BroadPhaseLayerMap m_broadPhaseLayers;
    ObjectVsBroadPhaseLayerFilterImpl m_objectVsBroadPhaseFilter;
    ObjectLayerPairFilterImpl m_objectLayerPairFilter;

    std::unique_ptr<JPH::TempAllocatorImpl> m_tempAllocator;
    std::unique_ptr<JPH::JobSystemThreadPool> m_jobSystem;
    std::unique_ptr<JPH::PhysicsSystem> m_system;
};

}
