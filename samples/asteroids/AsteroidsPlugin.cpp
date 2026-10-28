#include "AsteroidsPlugin.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <random>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <imgui.h>

#include "app/App.h"
#include "assets/AssetServer.h"
#include "ecs/Camera.h"
#include "ecs/Hierarchy.h"
#include "ecs/SceneLighting.h"
#include "ecs/Spawn.h"
#include "input/InputSystem.h"
#include "platform/PlatformPlugin.h"
#include "render/DefaultAssetFactory.h"
#include "render/MaterialInstance.h"
#include "render/Model.h"
#include "render/RenderAPI.h"
#include "render/Renderable.h"
#include "render/Skybox.h"
#include "render/Texture.h"
#include "ui/ImGuiPlugin.h"

namespace ailo {

namespace {

// The arena is a cube that wraps around on every axis. Everything is kept within half of it from the ship, so the
// ship always sees the closest copy of each rock; rocks shrink away near the far faces instead of popping.
constexpr float kArenaSize = 280.0f;
constexpr float kHalfArena = kArenaSize * 0.5f;
constexpr float kEdgeFade = 40.0f;

constexpr float kShipLength = 8.0f;
constexpr float kShipRadius = 2.6f;
constexpr float kThrust = 50.0f;
constexpr float kMaxSpeed = 120.0f;
constexpr float kDrag = 0.4f;
constexpr float kMouseSensitivity = 0.0025f; // radians per pixel
constexpr float kTurnResponse = 16.0f;       // how fast the ship catches up with the mouse, 1/s
constexpr float kFireInterval = 0.15f;
constexpr float kRespawnDelay = 2.0f;
constexpr float kRespawnClearance = 25.0f; // the ship waits until no rock is this close to its surface
constexpr float kInvulnerableTime = 3.0f;
constexpr int kStartLives = 3;
constexpr int kExtraLifeScore = 10000;

constexpr float kLaserSpeed = 170.0f;
constexpr float kLaserLife = 1.0f;

// Ship-local: behind (+Z) and above the ship.
constexpr glm::vec3 kCameraOffset { 0.0f, 10.0f, 24.0f };
constexpr float kCameraLookAhead = 30.0f;
constexpr float kCameraFollow = 4.5f;   // position smoothing, 1/s
constexpr float kCameraTurn = 4.0f;     // rotation smoothing, 1/s
constexpr float kCameraFov = glm::radians(60.0f);     // vertical, at rest
constexpr float kCameraFovFast = glm::radians(70.0f); // vertical, at kMaxSpeed
constexpr float kCameraFovResponse = 3.0f;            // fov smoothing, 1/s

constexpr float kWaveDelay = 2.5f;
constexpr float kWaveSpawnDistance = 90.0f;

struct RockSize {
    float radius;
    float minSpeed;
    float maxSpeed;
    int score;
    int sparks;
    int debris;
};

// Large, medium, small. Shooting one splits it into two of the next size; small ones are destroyed.
constexpr std::array<RockSize, 3> kRockSizes {{
    { 14.0f, 10.0f, 20.0f, 20, 40, 6 },
    { 7.0f, 18.0f, 28.0f, 50, 26, 4 },
    { 3.0f, 22.0f, 32.0f, 100, 16, 3 },
}};

const glm::vec4 kLaserColor { 0.6f, 3.0f, 8.0f, 1.0f };
const glm::vec4 kSparkColor { 8.0f, 3.2f, 0.8f, 1.0f };
const glm::vec4 kExhaustColor { 1.2f, 2.6f, 9.0f, 1.0f };
const glm::vec4 kShipSparkColor { 3.0f, 6.0f, 9.0f, 1.0f };

constexpr glm::vec3 kForward { 0.0f, 0.0f, -1.0f };
constexpr glm::vec3 kUp { 0.0f, 1.0f, 0.0f };

struct Ship {
    glm::vec3 velocity { 0.0f };
    glm::vec2 pendingTurn { 0.0f }; // mouse turn not applied yet: yaw, pitch
    float bank = 0.0f;
    float fireCooldown = 0.0f;
    float invulnerable = 0.0f;
    bool alive = true;
    bool thrusting = false;
    bool hidden = false;
    Entity bankNode = entt::null; // child that rolls into turns and blinks while invulnerable
};

struct Rock {
    glm::vec3 velocity { 0.0f };
    glm::vec3 spinAxis { 0.0f, 1.0f, 0.0f };
    float spinSpeed = 0.0f;
    float scale = 1.0f;  // of the root, before the edge fade
    int size = 0;        // index into kRockSizes
};

struct Laser {
    glm::vec3 velocity { 0.0f };
    float life = kLaserLife;
};

// Sparks, exhaust and rock debris: fly, slow down, spin and shrink to nothing.
struct Particle {
    glm::vec3 velocity { 0.0f };
    glm::vec3 spinAxis { 0.0f, 1.0f, 0.0f };
    float spinSpeed = 0.0f;
    glm::vec3 scale { 1.0f };
    float life = 1.0f;
    float maxLife = 1.0f;
    float drag = 1.0f;
};

// The game runs only while the cursor is captured.
struct AsteroidsInput {
    bool captured = false;
    int ignoreMouseFrames = 0;   // the cursor can jump when it is captured
    bool fireBlocked = false;    // the click that captured the cursor must not fire
};

struct MeshPart {
    AssetPtr<Mesh> mesh;
    AssetPtr<MaterialInstance> material;
    Transform local;
};

struct RockMesh {
    MeshPart part;
    float radius = 1.0f; // average half extent, in the root's space
};

float randomFloat(std::mt19937& rng, float lo, float hi) {
    return std::uniform_real_distribution<float>(lo, hi)(rng);
}

glm::vec3 randomDirection(std::mt19937& rng) {
    std::normal_distribution<float> normal;
    glm::vec3 v(normal(rng), normal(rng), normal(rng));
    const float length = glm::length(v);
    return length > 1e-6f ? v / length : kUp;
}

glm::quat randomRotation(std::mt19937& rng) {
    std::normal_distribution<float> normal;
    return glm::normalize(glm::quat(normal(rng), normal(rng), normal(rng), normal(rng)));
}

// The copy of p closest to center in the wrap-around arena.
glm::vec3 wrapNear(glm::vec3 p, glm::vec3 center) {
    const glm::vec3 d = p - center;
    return center + d - kArenaSize * glm::round(d / kArenaSize);
}

float edgeFade(glm::vec3 offset) {
    const glm::vec3 a = glm::abs(offset);
    return glm::clamp((kHalfArena - std::max({ a.x, a.y, a.z })) / kEdgeFade, 0.0f, 1.0f);
}

float lengthSq(glm::vec3 v) {
    return glm::dot(v, v);
}

float damp(float rate, float dt) {
    return 1.0f - std::exp(-rate * dt);
}

bool segmentHitsSphere(glm::vec3 a, glm::vec3 b, glm::vec3 center, float radius) {
    const glm::vec3 ab = b - a;
    const float abSq = lengthSq(ab);
    const float t = abSq > 0.0f ? glm::clamp(glm::dot(center - a, ab) / abSq, 0.0f, 1.0f) : 0.0f;
    return lengthSq(a + ab * t - center) <= radius * radius;
}

Aabb modelBounds(const Model& model) {
    Aabb bounds { glm::vec3(FLT_MAX), glm::vec3(-FLT_MAX) };
    for (const auto& instance : model.instances) {
        for (const glm::vec3& corner : instance.mesh->bounds.corners()) {
            const glm::vec3 p = glm::vec3(instance.transform * glm::vec4(corner, 1.0f));
            bounds.min = glm::min(bounds.min, p);
            bounds.max = glm::max(bounds.max, p);
        }
    }
    return bounds;
}

class AsteroidsGame {
public:
    int score = 0;
    int lives = kStartLives;
    int wave = 0;
    bool started = false;
    bool gameOver = false;
    float bannerTimer = 0.0f;

    void load(AssetServer& server, AssetStorage<MaterialInstance>& instances, RenderAPI& api) {
        auto pbr = server.load<Material>("assets/materials/pbr.mat");
        auto glow = server.load<Material>("assets/materials/glow.mat");

        auto texture = [&server](const std::string& key) { return server.load<Texture>(key); };
        auto glowInstance = [&](const glm::vec4& color) {
            auto instance = MaterialInstance::create(instances, server, api, glow);
            if (instance) instance->setParameter("color", color);
            return instance;
        };

        m_cube = server.load<Mesh>(meshes::kUnitCube);
        m_laserMaterial = glowInstance(kLaserColor);
        m_sparkMaterial = glowInstance(kSparkColor);
        m_exhaustMaterial = glowInstance(kExhaustColor);
        m_shipSparkMaterial = glowInstance(kShipSparkColor);

        if (auto ship = server.load<Model>("assets/models/ship/vaisso.fbx.model"); ship && !ship->instances.empty()) {
            const Aabb bounds = modelBounds(*ship);
            const glm::vec3 size = bounds.max - bounds.min;
            const float scale = kShipLength / std::max({ size.x, size.y, size.z });
            m_shipFrame = Transform { .position = -bounds.center() * scale, .scale = glm::vec3(scale) };

            // Two meshes: the hull ("A" textures) and the cockpit glass, the smaller one.
            auto indexCount = [](const Mesh& mesh) {
                uint32_t count = 0;
                for (const auto& face : mesh.faces) count += face.indexCount;
                return count;
            };
            size_t hull = 0;
            for (size_t i = 1; i < ship->instances.size(); i++) {
                if (indexCount(*ship->instances[i].mesh) > indexCount(*ship->instances[hull].mesh)) hull = i;
            }

            for (size_t i = 0; i < ship->instances.size(); i++) {
                const std::string prefix = "assets/models/ship/" + std::string(i == hull ? "A" : "Cockpit");
                auto material = MaterialInstance::create(instances, server, api, pbr);
                if (material) {
                    material->setParameter("baseColorMap", texture(prefix + "_Base_Color.png.tex"));
                    material->setParameter("normalMap", texture(prefix + "_Normal_DirectX.png.tex"));
                    material->setParameter("metallicMap", texture(prefix + "_Metallic.png.tex"));
                    material->setParameter("roughnessMap", texture(prefix + "_Roughness.png.tex"));
                    material->setParameter("occlusionMap", texture(prefix + "_Mixed_AO.png.tex"));
                    material->setParameter("normalGreenSign", -1.0f);
                    if (i == hull) {
                        material->setParameter("emissiveMap", texture("assets/models/ship/A_Emissive.png.tex"));
                        m_hullMaterial = material;
                    }
                }
                m_shipParts.push_back(MeshPart {
                    .mesh = ship->instances[i].mesh,
                    .material = material,
                    .local = Transform::fromMatrix(ship->instances[i].transform),
                });
            }
        }

        if (auto rocks = server.load<Model>("assets/models/asteroids/asteroid_pack_01.obj.model")) {
            constexpr auto kPrefix = "assets/models/asteroids/asteroid_pack_01_DefaultMaterial_";
            m_rockMaterial = MaterialInstance::create(instances, server, api, pbr);
            if (m_rockMaterial) {
                m_rockMaterial->setParameter("baseColorMap", texture(std::string(kPrefix) + "BaseColor.png.tex"));
                m_rockMaterial->setParameter("normalMap", texture(std::string(kPrefix) + "Normal_Ut.png.tex"));
                m_rockMaterial->setParameter("metallicMap", texture(std::string(kPrefix) + "Metallic_.png.tex"));
                m_rockMaterial->setParameter("roughnessMap", texture(std::string(kPrefix) + "Roughness.png.tex"));
            }

            // Every rock is centered on its own origin (scripts/split_asteroid_pack.py).
            for (const auto& instance : rocks->instances) {
                const Transform local = Transform::fromMatrix(instance.transform);
                const glm::vec3 extent = instance.mesh->bounds.extent() * local.scale;
                m_rockMeshes.push_back(RockMesh {
                    .part = { .mesh = instance.mesh, .material = m_rockMaterial, .local = local },
                    .radius = (extent.x + extent.y + extent.z) / 3.0f,
                });
            }
        }
    }

    void setupScene(Scene& scene, AssetServer& server) {
        m_camera = scene::spawn(scene,
            TransformComponent {},
            Camera { .fovY = kCameraFov, .nearPlane = 0.5f, .farPlane = 1200.0f, .exposure = 1.2f },
            Skybox { .cubemap = server.load<Texture>("assets/textures/nebula_skybox.tex") },
            SceneLighting {
                .indirectLight = server.load<IndirectLight>("assets/textures/nebula.light"),
                .lightDirection = glm::vec3(1.0f, 0.0f, 0.0f),
                .sunIntensity = 6.0f,
                .iblIntensity = 1.5f,
            });

        spawnShip(scene);
        snapCamera(scene);
        startWave(scene, 1);
    }

    void restart(Scene& scene) {
        auto& registry = scene.registry();
        std::vector<Entity> doomed;
        for (auto entity : registry.view<Rock>()) doomed.push_back(entity);
        for (auto entity : registry.view<Laser>()) doomed.push_back(entity);
        for (auto entity : registry.view<Particle>()) doomed.push_back(entity);
        for (auto entity : doomed) hierarchy::destroySubtree(registry, entity);

        score = 0;
        lives = kStartLives;
        gameOver = false;
        m_nextExtraLife = kExtraLifeScore;
        m_waveTimer = -1.0f;
        respawnShip(scene);
        startWave(scene, 1);
    }

    void update(Scene& scene, const InputSystem& keys, AsteroidsInput& input, float dt) {
        if (!input.captured) {
            return;
        }

        double dx = 0.0, dy = 0.0;
        keys.getMouseDelta(dx, dy);
        glm::vec2 mouse(static_cast<float>(dx), static_cast<float>(dy));
        if (input.ignoreMouseFrames > 0) {
            input.ignoreMouseFrames--;
            mouse = glm::vec2(0.0f);
        }

        auto& registry = scene.registry();
        updateShip(scene, keys, input, mouse, dt);
        recenter(scene);

        const glm::vec3 shipPosition = this->shipPosition(scene);
        updateRocks(registry, shipPosition, dt);
        updateLasers(registry, shipPosition, dt);
        updateParticles(registry, shipPosition, dt);
        collide(scene, dt);
        updateFlow(scene, dt);
    }

    // Trails the ship from behind and above, smoothing both position and rotation, so turns and thrust swing the
    // view a little before it catches up.
    void updateCamera(Scene& scene, float dt) {
        auto& registry = scene.registry();
        const Transform& ship = registry.get<TransformComponent>(m_ship).world();

        const glm::vec3 desired = ship.position + ship.rotation * kCameraOffset;
        m_cameraPose.position = glm::mix(m_cameraPose.position, desired, damp(kCameraFollow, dt));

        const glm::vec3 target = ship.position + ship.rotation * kForward * kCameraLookAhead;
        const glm::quat look = glm::quatLookAt(glm::normalize(target - m_cameraPose.position), ship.rotation * kUp);
        m_cameraPose.rotation = glm::normalize(glm::slerp(m_cameraPose.rotation, look, damp(kCameraTurn, dt)));

        const float speed = std::min(glm::length(registry.get<Ship>(m_ship).velocity) / kMaxSpeed, 1.0f);
        float& fov = registry.get<Camera>(m_camera).fovY;
        fov = glm::mix(fov, glm::mix(kCameraFov, kCameraFovFast, speed), damp(kCameraFovResponse, dt));

        // The shake is not kept in the smoothed pose, so the camera settles back.
        m_shake = std::max(0.0f, m_shake - dt * 2.5f);
        Transform pose = m_cameraPose;
        pose.position += randomDirection(m_rng) * (m_shake * m_shake * 1.5f);
        hierarchy::setWorld(registry, m_camera, pose);
    }

    void drawHud(Scene& scene, const AsteroidsInput& input, float aspect);

private:
    std::mt19937 m_rng { std::random_device {}() };

    AssetPtr<Mesh> m_cube;
    AssetPtr<MaterialInstance> m_laserMaterial;
    AssetPtr<MaterialInstance> m_sparkMaterial;
    AssetPtr<MaterialInstance> m_exhaustMaterial;
    AssetPtr<MaterialInstance> m_shipSparkMaterial;
    AssetPtr<MaterialInstance> m_hullMaterial;
    AssetPtr<MaterialInstance> m_rockMaterial;
    std::vector<MeshPart> m_shipParts;
    std::vector<RockMesh> m_rockMeshes;
    Transform m_shipFrame; // centers and scales the ship model

    Entity m_ship = entt::null;
    Entity m_camera = entt::null;
    Transform m_cameraPose; // smoothed, without the shake
    float m_shake = 0.0f;
    float m_respawnTimer = 0.0f;
    float m_waveTimer = -1.0f;
    float m_exhaustTimer = 0.0f;
    int m_nextExtraLife = kExtraLifeScore;

    static Entity spawnPart(Scene& scene, const MeshPart& part, Entity parent, bool castShadows = true) {
        const Entity entity = scene.addEntity();
        scene.addComponent<TransformComponent>(entity, part.local);
        Renderable& renderable = scene.addComponent<Renderable>(entity);
        renderable.mesh = part.mesh;
        renderable.materials.assign(part.mesh->faces.size(), part.material);
        renderable.castShadows = castShadows;
        hierarchy::setParent(scene.registry(), entity, parent, hierarchy::Keep::Local);
        return entity;
    }

    Entity spawnGlowCube(Scene& scene, const AssetPtr<MaterialInstance>& material, const Transform& transform) {
        const Entity entity = scene.addEntity();
        scene.addComponent<TransformComponent>(entity, transform);
        Renderable& renderable = scene.addComponent<Renderable>(entity);
        renderable.mesh = m_cube;
        renderable.materials.assign(m_cube->faces.size(), material);
        renderable.castShadows = false;
        renderable.receiveShadows = false;
        return entity;
    }

    // A root at the transform with one child mesh of the given rock.
    Entity spawnRockMesh(Scene& scene, const RockMesh& mesh, const Transform& transform, bool castShadows) {
        const Entity root = scene.addEntity();
        scene.addComponent<TransformComponent>(root, transform);
        spawnPart(scene, mesh.part, root, castShadows);
        return root;
    }

    void spawnShip(Scene& scene) {
        auto& registry = scene.registry();
        m_ship = scene.addEntity();
        scene.addComponent<TransformComponent>(m_ship, Transform { .rotation = glm::quat({0, 1, 0}, {-1, 0, 0}) });

        const Entity bank = scene.addEntity();
        scene.addComponent<TransformComponent>(bank, Transform {});
        hierarchy::setParent(registry, bank, m_ship, hierarchy::Keep::Local);

        const Entity frame = scene.addEntity();
        scene.addComponent<TransformComponent>(frame, m_shipFrame);
        hierarchy::setParent(registry, frame, bank, hierarchy::Keep::Local);

        for (const auto& part : m_shipParts) {
            spawnPart(scene, part, frame);
        }

        scene.addComponent<Ship>(m_ship, Ship { .invulnerable = kInvulnerableTime, .bankNode = bank });
    }

    void respawnShip(Scene& scene) {
        Ship& ship = scene.get<Ship>(m_ship);
        ship.alive = true;
        ship.velocity = glm::vec3(0.0f);
        ship.pendingTurn = glm::vec2(0.0f);
        ship.invulnerable = kInvulnerableTime;
        ship.fireCooldown = 0.0f;
    }

    glm::vec3 shipPosition(Scene& scene) const {
        return scene.registry().get<TransformComponent>(m_ship).world().position;
    }

    void snapCamera(Scene& scene) {
        auto& registry = scene.registry();
        const Transform& ship = registry.get<TransformComponent>(m_ship).world();
        const glm::vec3 position = ship.position + ship.rotation * kCameraOffset;
        const glm::vec3 target = ship.position + ship.rotation * kForward * kCameraLookAhead;
        m_cameraPose = Transform {
            .position = position,
            .rotation = glm::quatLookAt(glm::normalize(target - position), ship.rotation * kUp),
        };
        hierarchy::setWorld(registry, m_camera, m_cameraPose);
    }

    void spawnRock(Scene& scene, int size, glm::vec3 position, glm::vec3 velocity) {
        if (m_rockMeshes.empty()) return;
        const RockMesh& mesh = m_rockMeshes[std::uniform_int_distribution<size_t>(0, m_rockMeshes.size() - 1)(m_rng)];
        const RockSize& rockSize = kRockSizes[size];
        const float scale = rockSize.radius / mesh.radius * randomFloat(m_rng, 0.85f, 1.15f);

        const Entity root = spawnRockMesh(scene, mesh, Transform {
            .position = position,
            .rotation = randomRotation(m_rng),
            .scale = glm::vec3(scale),
        }, true);
        scene.addComponent<Rock>(root, Rock {
            .velocity = velocity,
            .spinAxis = randomDirection(m_rng),
            .spinSpeed = randomFloat(m_rng, 0.2f, 1.2f) * (1.0f + size * 0.6f),
            .scale = scale,
            .size = size,
        });
    }

    void startWave(Scene& scene, int number) {
        wave = number;
        bannerTimer = 2.5f;
        const glm::vec3 center = shipPosition(scene);
        const int count = std::min(6 + 3 * number, 32);
        for (int i = 0; i < count; i++) {
            glm::vec3 offset;
            do {
                offset = glm::vec3(randomFloat(m_rng, -kHalfArena, kHalfArena), randomFloat(m_rng, -kHalfArena, kHalfArena),
                                   randomFloat(m_rng, -kHalfArena, kHalfArena)) * 0.8f;
            } while (glm::length(offset) < kWaveSpawnDistance);
            const RockSize& size = kRockSizes[0];
            const float speed = randomFloat(m_rng, size.minSpeed, size.maxSpeed) * (1.0f + 0.08f * (number - 1));
            spawnRock(scene, 0, center + offset, randomDirection(m_rng) * speed);
        }
    }

    void spawnParticle(Scene& scene, const AssetPtr<MaterialInstance>& material, glm::vec3 position, glm::vec3 velocity,
                       float size, float life, float drag) {
        const glm::vec3 scale(size);
        const Entity entity = spawnGlowCube(scene, material, Transform {
            .position = position,
            .rotation = randomRotation(m_rng),
            .scale = scale,
        });
        scene.addComponent(entity, Particle {
            .velocity = velocity,
            .spinAxis = randomDirection(m_rng),
            .spinSpeed = randomFloat(m_rng, 2.0f, 8.0f),
            .scale = scale,
            .life = life,
            .maxLife = life,
            .drag = drag,
        });
    }

    void explode(Scene& scene, glm::vec3 position, glm::vec3 velocity, float radius, int sparks, int debris,
                 const AssetPtr<MaterialInstance>& sparkMaterial) {
        for (int i = 0; i < sparks; i++) {
            const glm::vec3 direction = randomDirection(m_rng);
            spawnParticle(scene, sparkMaterial, position + direction * radius * 0.5f,
                velocity * 0.5f + direction * randomFloat(m_rng, 8.0f, 30.0f) * (0.5f + radius * 0.1f),
                randomFloat(m_rng, 0.25f, 0.6f) * (0.6f + radius * 0.1f), randomFloat(m_rng, 0.4f, 1.1f), 2.0f);
        }

        for (int i = 0; i < debris && !m_rockMeshes.empty(); i++) {
            const RockMesh& mesh = m_rockMeshes[std::uniform_int_distribution<size_t>(0, m_rockMeshes.size() - 1)(m_rng)];
            const glm::vec3 direction = randomDirection(m_rng);
            const glm::vec3 scale(radius * randomFloat(m_rng, 0.12f, 0.25f) / mesh.radius);
            const Entity root = spawnRockMesh(scene, mesh, Transform {
                .position = position + direction * radius * 0.4f,
                .rotation = randomRotation(m_rng),
                .scale = scale,
            }, false);
            const float life = randomFloat(m_rng, 1.2f, 2.2f);
            scene.addComponent(root, Particle {
                .velocity = velocity + direction * randomFloat(m_rng, 4.0f, 14.0f),
                .spinAxis = randomDirection(m_rng),
                .spinSpeed = randomFloat(m_rng, 1.0f, 4.0f),
                .scale = scale,
                .life = life,
                .maxLife = life,
                .drag = 0.5f,
            });
        }
    }

    void updateShip(Scene& scene, const InputSystem& keys, AsteroidsInput& input, glm::vec2 mouse, float dt) {
        auto& registry = scene.registry();
        Ship& ship = registry.get<Ship>(m_ship);
        Transform transform = registry.get<TransformComponent>(m_ship).world();

        float yawRate = 0.0f;
        if (ship.alive) {
            ship.pendingTurn += mouse * kMouseSensitivity;
            const glm::vec2 turn = ship.pendingTurn * damp(kTurnResponse, dt);
            ship.pendingTurn -= turn;
            yawRate = dt > 0.0f ? turn.x / dt : 0.0f;

            // Mouse right turns right, mouse up pitches the nose up, around the ship's own axes.
            transform.rotation = glm::normalize(transform.rotation
                * glm::angleAxis(-turn.x, kUp)
                * glm::angleAxis(-turn.y, glm::vec3(1.0f, 0.0f, 0.0f)));

            ship.thrusting = keys.isKeyPressed(KeyCode::W);
            const glm::vec3 forward = transform.rotation * kForward;
            if (ship.thrusting) {
                ship.velocity += forward * kThrust * dt;
            }
            ship.velocity *= std::exp(-kDrag * dt);
            if (const float speed = glm::length(ship.velocity); speed > kMaxSpeed) {
                ship.velocity *= kMaxSpeed / speed;
            }

            bool isShooting = keys.isMouseButtonPressed(MouseButton::Left) || keys.isKeyPressed(KeyCode::Space);
            if (!isShooting) {
                input.fireBlocked = false;
            }
            ship.fireCooldown = std::max(0.0f, ship.fireCooldown - dt);
            if (isShooting && !input.fireBlocked && ship.fireCooldown <= 0.0f) {
                ship.fireCooldown = kFireInterval;
                fire(scene, transform, ship.velocity);
            }

            ship.invulnerable = std::max(0.0f, ship.invulnerable - dt);
        } else {
            ship.thrusting = false;
            ship.velocity *= std::exp(-2.0f * dt);
        }

        transform.position += ship.velocity * dt;
        hierarchy::setWorld(registry, m_ship, transform);

        // Roll into turns; hidden while dead, blinking while invulnerable.
        ship.bank = glm::mix(ship.bank, glm::clamp(-yawRate * 0.25f, -0.9f, 0.9f), damp(6.0f, dt));
        const bool blinkOff = ship.invulnerable > 0.0f && std::fmod(ship.invulnerable, 0.25f) < 0.1f;
        hierarchy::setLocal(registry, ship.bankNode, Transform { .rotation = glm::angleAxis(ship.bank, -kForward) });
        if (const bool hidden = !ship.alive || blinkOff; hidden != ship.hidden) {
            ship.hidden = hidden;
            if (hidden) {
                hierarchy::disableSubtree(registry, ship.bankNode);
            } else {
                hierarchy::enableSubtree(registry, ship.bankNode);
            }
        }

        if (m_hullMaterial) {
            m_hullMaterial->setParameter("emissiveFactor", glm::vec3(ship.thrusting ? 6.0f : 2.0f));
        }

        if (ship.thrusting) {
            m_exhaustTimer -= dt;
            const glm::vec3 back = transform.rotation * -kForward;
            while (m_exhaustTimer <= 0.0f) {
                m_exhaustTimer += 0.015f;
                const glm::vec3 jitter = transform.rotation * glm::vec3(randomFloat(m_rng, -0.7f, 0.7f),
                                                                       randomFloat(m_rng, -0.3f, 0.3f), 0.0f);
                spawnParticle(scene, m_exhaustMaterial, transform.position + back * (kShipLength * 0.5f) + jitter,
                    ship.velocity + back * randomFloat(m_rng, 15.0f, 25.0f) + randomDirection(m_rng) * 2.0f,
                    randomFloat(m_rng, 0.15f, 0.3f), randomFloat(m_rng, 0.2f, 0.35f), 1.0f);
            }
        }
    }

    void fire(Scene& scene, const Transform& ship, glm::vec3 shipVelocity) {
        const glm::vec3 forward = ship.rotation * kForward;
        const Entity laser = spawnGlowCube(scene, m_laserMaterial, Transform {
            .position = ship.position + forward * (kShipLength * 0.6f),
            .rotation = ship.rotation,
            .scale = { 0.2f, 0.2f, 3.5f },
        });
        scene.addComponent(laser, Laser { .velocity = shipVelocity + forward * kLaserSpeed });
    }

    // Keeps the ship (and the camera with it) inside the arena by moving them a whole arena size, which changes
    // nothing in a wrap-around world. Everything else is wrapped relative to the ship anyway.
    void recenter(Scene& scene) {
        auto& registry = scene.registry();
        const glm::vec3 position = shipPosition(scene);
        const glm::vec3 shift = -kArenaSize * glm::round(position / kArenaSize);
        if (shift == glm::vec3(0.0f)) return;

        Transform ship = registry.get<TransformComponent>(m_ship).world();
        ship.position += shift;
        hierarchy::setWorld(registry, m_ship, ship);
        m_cameraPose.position += shift;
    }

    // Rocks at the far faces are disabled (and so not drawn) instead of scaled to zero. The view includes disabled
    // rocks, which keep moving.
    void updateRocks(entt::registry& registry, glm::vec3 center, float dt) {
        for (auto&& [entity, rock, transform] : registry.view<Rock, TransformComponent>().each()) {
            const Transform& world = transform.world();
            const glm::vec3 position = wrapNear(world.position + rock.velocity * dt, center);
            const float fade = edgeFade(position - center);
            if (const bool hidden = fade <= 0.0f; hidden != registry.all_of<Disabled>(entity)) {
                if (hidden) {
                    hierarchy::disableSubtree(registry, entity);
                } else {
                    hierarchy::enableSubtree(registry, entity);
                }
            }
            hierarchy::setWorld(registry, entity, Transform {
                .position = position,
                .rotation = glm::normalize(glm::angleAxis(rock.spinSpeed * dt, rock.spinAxis) * world.rotation),
                .scale = fade > 0.0f ? glm::vec3(rock.scale * fade) : world.scale,
            });
        }
    }

    void updateLasers(entt::registry& registry, glm::vec3 center, float dt) {
        std::vector<Entity> expired;
        for (auto&& [entity, laser, transform] : registry.view<Laser, TransformComponent>().each()) {
            laser.life -= dt;
            if (laser.life <= 0.0f) {
                expired.push_back(entity);
                continue;
            }
            Transform world = transform.world();
            world.position = wrapNear(world.position + laser.velocity * dt, center);
            hierarchy::setWorld(registry, entity, world);
        }
        for (const Entity entity : expired) registry.destroy(entity);
    }

    void updateParticles(entt::registry& registry, glm::vec3 center, float dt) {
        std::vector<Entity> expired;
        for (auto&& [entity, particle, transform] : registry.view<Particle, TransformComponent>().each()) {
            particle.life -= dt;
            if (particle.life <= 0.0f) {
                expired.push_back(entity);
                continue;
            }
            particle.velocity *= std::exp(-particle.drag * dt);
            const Transform& world = transform.world();
            const float t = particle.life / particle.maxLife;
            hierarchy::setWorld(registry, entity, Transform {
                .position = wrapNear(world.position + particle.velocity * dt, center),
                .rotation = glm::normalize(glm::angleAxis(particle.spinSpeed * dt, particle.spinAxis) * world.rotation),
                .scale = particle.scale * std::sqrt(t),
            });
        }
        for (const Entity entity : expired) hierarchy::destroySubtree(registry, entity);
    }

    void addScore(int points) {
        score += points;
        while (score >= m_nextExtraLife) {
            lives++;
            m_nextExtraLife += kExtraLifeScore;
        }
    }

    void collide(Scene& scene, float dt) {
        auto& registry = scene.registry();

        struct Hit { Entity rock; Entity laser; };
        std::vector<Hit> hits;
        for (auto&& [laserEntity, laser, laserTransform] : registry.view<Laser, TransformComponent>().each()) {
            const Transform& tip = laserTransform.world();
            const glm::vec3 direction = glm::normalize(laser.velocity);
            // The whole beam and the distance it covered this frame.
            const glm::vec3 front = tip.position + direction * (tip.scale.z * 0.5f);
            const glm::vec3 back = tip.position - laser.velocity * dt - direction * (tip.scale.z * 0.5f);
            for (auto&& [rockEntity, rock, rockTransform] : registry.view<Rock, TransformComponent>().each()) {
                if (segmentHitsSphere(back, front, rockTransform.world().position, kRockSizes[rock.size].radius)) {
                    hits.push_back({ rockEntity, laserEntity });
                    break;
                }
            }
        }

        for (const Hit& hit : hits) {
            if (!registry.valid(hit.rock) || !registry.valid(hit.laser)) continue;
            const Rock rock = registry.get<Rock>(hit.rock);
            const glm::vec3 position = registry.get<TransformComponent>(hit.rock).world().position;
            const glm::vec3 laserVelocity = registry.get<Laser>(hit.laser).velocity;
            registry.destroy(hit.laser);
            hierarchy::destroySubtree(registry, hit.rock);
            destroyRock(scene, rock, position, laserVelocity);
        }

        Ship& ship = registry.get<Ship>(m_ship);
        if (!ship.alive || ship.invulnerable > 0.0f) return;

        const glm::vec3 shipPosition = this->shipPosition(scene);
        Entity crashed = entt::null;
        for (auto&& [rockEntity, rock, rockTransform] : registry.view<Rock, TransformComponent>().each()) {
            const float reach = kShipRadius + kRockSizes[rock.size].radius * 0.85f;
            if (lengthSq(rockTransform.world().position - shipPosition) < reach * reach) {
                crashed = rockEntity;
                break;
            }
        }
        if (crashed != entt::null) {
            const Rock rock = registry.get<Rock>(crashed);
            const glm::vec3 position = registry.get<TransformComponent>(crashed).world().position;
            hierarchy::destroySubtree(registry, crashed);
            destroyRock(scene, rock, position, ship.velocity);
            killShip(scene);
        }
    }

    void destroyRock(Scene& scene, const Rock& rock, glm::vec3 position, glm::vec3 impact) {
        const RockSize& size = kRockSizes[rock.size];
        addScore(size.score);
        explode(scene, position, rock.velocity, size.radius, size.sparks, size.debris, m_sparkMaterial);
        const float distance = std::max(glm::distance(position, shipPosition(scene)), 1.0f);
        m_shake = std::max(m_shake, std::min(size.radius * 4.0f / distance, 1.0f) * 0.6f);

        if (rock.size + 1 >= int(kRockSizes.size())) return;

        const int childSize = rock.size + 1;
        const RockSize& child = kRockSizes[childSize];
        // The two halves fly apart across the shot, plus a little of its push.
        const glm::vec3 push = lengthSq(impact) > 0.0f ? glm::normalize(impact) : randomDirection(m_rng);
        glm::vec3 across = glm::cross(push, randomDirection(m_rng));
        across = lengthSq(across) > 1e-6f ? glm::normalize(across) : randomDirection(m_rng);
        for (const float side : { -1.0f, 1.0f }) {
            const glm::vec3 direction = glm::normalize(across * side + randomDirection(m_rng) * 0.35f + push * 0.25f);
            const float speed = randomFloat(m_rng, child.minSpeed, child.maxSpeed);
            spawnRock(scene, childSize, position + direction * child.radius * 0.8f,
                rock.velocity * 0.5f + direction * speed);
        }
    }

    void killShip(Scene& scene) {
        Ship& ship = scene.get<Ship>(m_ship);
        ship.alive = false;
        lives--;
        m_respawnTimer = kRespawnDelay;
        m_shake = 1.0f;
        const glm::vec3 position = shipPosition(scene);
        explode(scene, position, ship.velocity, 3.0f, 70, 0, m_shipSparkMaterial);
        explode(scene, position, ship.velocity, 2.0f, 40, 0, m_sparkMaterial);
        if (lives <= 0) {
            gameOver = true;
        }
    }

    bool spawnAreaClear(Scene& scene) {
        auto& registry = scene.registry();
        const glm::vec3 shipPosition = this->shipPosition(scene);
        for (auto&& [entity, rock, transform] : registry.view<Rock, TransformComponent>().each()) {
            const float reach = kShipRadius + kRockSizes[rock.size].radius + kRespawnClearance;
            if (lengthSq(transform.world().position - shipPosition) < reach * reach) return false;
        }
        return true;
    }

    void updateFlow(Scene& scene, float dt) {
        bannerTimer = std::max(0.0f, bannerTimer - dt);

        Ship& ship = scene.get<Ship>(m_ship);
        if (!ship.alive && !gameOver) {
            m_respawnTimer -= dt;
            if (m_respawnTimer <= 0.0f && spawnAreaClear(scene)) respawnShip(scene);
        }

        if (gameOver) return;
        if (scene.registry().view<Rock>().empty()) {
            if (m_waveTimer < 0.0f) {
                m_waveTimer = kWaveDelay;
            } else if ((m_waveTimer -= dt) <= 0.0f) {
                m_waveTimer = -1.0f;
                startWave(scene, wave + 1);
            }
        }
    }
};

void AsteroidsGame::drawHud(Scene& scene, const AsteroidsInput& input, float aspect) {
    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 display = io.DisplaySize;
    if (display.x <= 0.0f || display.y <= 0.0f) return;

    ImDrawList* draw = ImGui::GetForegroundDrawList();
    ImFont* font = ImGui::GetFont();
    const float unit = display.y / 1080.0f;

    auto text = [&](ImVec2 position, float size, ImU32 color, const std::string& string, bool centered) {
        const float px = size * unit;
        const ImVec2 extent = font->CalcTextSizeA(px, FLT_MAX, 0.0f, string.c_str());
        if (centered) position.x -= extent.x * 0.5f;
        draw->AddText(font, px, ImVec2(position.x + 2.0f * unit, position.y + 2.0f * unit), IM_COL32(0, 0, 0, 160), string.c_str());
        draw->AddText(font, px, position, color, string.c_str());
    };

    const ImU32 white = IM_COL32(235, 240, 255, 255);
    const ImU32 accent = IM_COL32(120, 200, 255, 255);

    text(ImVec2(30 * unit, 24 * unit), 40, white, std::to_string(score), false);
    text(ImVec2(30 * unit, 70 * unit), 22, accent, "WAVE " + std::to_string(wave), false);

    // Lives as little ships.
    for (int i = 0; i < lives; i++) {
        const float x = (40.0f + i * 28.0f) * unit;
        const float y = 120.0f * unit;
        draw->AddTriangleFilled(ImVec2(x, y - 12 * unit), ImVec2(x - 9 * unit, y + 10 * unit), ImVec2(x + 9 * unit, y + 10 * unit), white);
    }

    auto& registry = scene.registry();
    const glm::vec2 center(display.x * 0.5f, display.y * 0.5f);

    // Crosshair where the ship's nose points, far ahead.
    if (input.captured && !gameOver && registry.valid(m_ship) && registry.valid(m_camera) && aspect > 0.0f) {
        const Ship& ship = registry.get<Ship>(m_ship);
        const Transform& shipTransform = registry.get<TransformComponent>(m_ship).world();
        const Transform& cameraTransform = registry.get<TransformComponent>(m_camera).world();
        const Camera& camera = registry.get<Camera>(m_camera);
        const glm::vec3 aim = shipTransform.position + shipTransform.rotation * kForward * 150.0f;
        const glm::vec4 clip = camera.projection(aspect) * glm::inverse(cameraTransform.toMatrix()) * glm::vec4(aim, 1.0f);
        if (ship.alive && clip.w > 0.0f) {
            const ImVec2 p((clip.x / clip.w * 0.5f + 0.5f) * display.x, (clip.y / clip.w * 0.5f + 0.5f) * display.y);
            const float r = 14.0f * unit;
            const ImU32 color = IM_COL32(140, 220, 255, 200);
            draw->AddCircle(p, r, color, 32, 2.0f * unit);
            draw->AddLine(ImVec2(p.x - r * 1.8f, p.y), ImVec2(p.x - r * 0.6f, p.y), color, 2.0f * unit);
            draw->AddLine(ImVec2(p.x + r * 0.6f, p.y), ImVec2(p.x + r * 1.8f, p.y), color, 2.0f * unit);
            draw->AddLine(ImVec2(p.x, p.y - r * 1.8f), ImVec2(p.x, p.y - r * 0.6f), color, 2.0f * unit);
            draw->AddLine(ImVec2(p.x, p.y + r * 0.6f), ImVec2(p.x, p.y + r * 1.8f), color, 2.0f * unit);
        }
    }

    if (gameOver) {
        draw->AddRectFilled(ImVec2(0, center.y - 130 * unit), ImVec2(display.x, center.y + 110 * unit), IM_COL32(0, 0, 0, 120));
        text(ImVec2(center.x, center.y - 100 * unit), 90, IM_COL32(255, 120, 90, 255), "GAME OVER", true);
        text(ImVec2(center.x, center.y + 10 * unit), 30, white, "Score " + std::to_string(score), true);
        text(ImVec2(center.x, center.y + 55 * unit), 24, accent,
             input.captured ? "Click or press Enter to play again" : "Click to play again", true);
    } else if (!input.captured) {
        draw->AddRectFilled(ImVec2(0, center.y - 170 * unit), ImVec2(display.x, center.y + 170 * unit), IM_COL32(0, 0, 0, 130));
        text(ImVec2(center.x, center.y - 140 * unit), 100, white, started ? "PAUSED" : "ASTEROIDS", true);
        text(ImVec2(center.x, center.y - 10 * unit), 30, accent, started ? "Click to resume" : "Click to start", true);
        text(ImVec2(center.x, center.y + 50 * unit), 22, white, "Mouse: steer    LMB / Space: fire    W: thrust", true);
        text(ImVec2(center.x, center.y + 85 * unit), 22, white, "Esc: pause    Esc while paused: quit", true);
    } else if (bannerTimer > 0.0f) {
        const int alpha = int(255.0f * std::min(1.0f, bannerTimer));
        text(ImVec2(center.x, center.y - 220 * unit), 60, IM_COL32(255, 255, 255, alpha), "WAVE " + std::to_string(wave), true);
    }
}

void setup(AsteroidsGame& game, World& world, AssetServer& server, AssetStorage<MaterialInstance>& instances,
           RenderAPI& api) {
    game.load(server, instances, api);
    game.setupScene(world.scene(), server);
}

void setCaptured(AsteroidsInput& input, Platform& platform, Window& window, bool captured) {
    platform.setCursorCaptured(window.handle(), captured);
    input.captured = captured;
    input.ignoreMouseFrames = captured ? 2 : 0;
}

// Esc pauses (releasing the cursor), and quits when already paused. A click captures the cursor and plays; once the
// game is over, a click or Enter restarts it.
void handleInput(AsteroidsGame& game, AsteroidsInput& input, const InputSystem& keys, Platform& platform,
                 Window& window, AppControl& control, World& world) {
    const bool clicked = keys.mouseButtonPressedThisFrame(MouseButton::Left);

    if (keys.pressedThisFrame(KeyCode::Escape)) {
        if (input.captured) {
            setCaptured(input, platform, window, false);
        } else {
            control.requestExit();
        }
        return;
    }

    const bool restart = clicked || keys.pressedThisFrame(KeyCode::Enter) || keys.pressedThisFrame(KeyCode::NumpadEnter);
    if (clicked && !input.captured) {
        setCaptured(input, platform, window, true);
        input.fireBlocked = true;
        game.started = true;
        if (game.gameOver) game.restart(world.scene());
    } else if (restart && input.captured && game.gameOver) {
        input.fireBlocked = true;
        game.restart(world.scene());
    }
}

void update(AsteroidsGame& game, AsteroidsInput& input, const InputSystem& keys, const Time& time, World& world) {
    const float dt = std::min(time.delta, 0.05f);
    game.update(world.scene(), keys, input, dt);
    if (input.captured) {
        game.updateCamera(world.scene(), dt);
    }
}

void drawHud(AsteroidsGame& game, const AsteroidsInput& input, const Window& window, World& world) {
    game.drawHud(world.scene(), input, window.aspect());
}

}

void AsteroidsPlugin::build(App& app) {
    app.addPlugin(ImGuiPlugin {});

    // Inserted after the asset storages, so the game's asset references are dropped before them.
    app.insertResource<AsteroidsGame>();
    app.insertResource<AsteroidsInput>();

    app.addSystem(Stage::Startup, setup, "Asteroids::setup");
    app.addSystem(Stage::Update, handleInput, "Asteroids::handleInput");
    app.addSystem(Stage::Update, update, "Asteroids::update");
    app.addSystem(Stage::Update, drawHud, "Asteroids::drawHud");
}

}
