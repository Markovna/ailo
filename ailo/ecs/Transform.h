#pragma once
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include "entt/entity/entity.hpp"
#include "entt/entity/fwd.hpp"

namespace ailo {

struct Transform {
    glm::vec3 position { 0.0f };
    glm::quat rotation = glm::identity<glm::quat>();
    glm::vec3 scale { 1.0f };

    glm::mat4 toMatrix() const;
    static Transform fromMatrix(const glm::mat4& m);
};

Transform compose(const Transform& parent, const Transform& local);

// The exact inverse of compose: the local that gives `world` under `parent`. On an axis where parent.scale is zero no
// local reaches `world`; scale and position on that axis are taken from `fallback` instead.
Transform relativeTo(const Transform& parent, const Transform& world, const Transform& fallback = {});

// Every field is read-only: writes go through ecs/Hierarchy.h
class TransformComponent {
public:
    TransformComponent() = default;
    explicit TransformComponent(const Transform& local) : m_local(local), m_world(local) {}

    const Transform& local() const { return m_local; }
    const Transform& world() const { return m_world; }

    entt::entity parent() const { return m_parent; }
    entt::entity firstChild() const { return m_firstChild; }
    entt::entity nextSibling() const { return m_next; }
    entt::entity prevSibling() const { return m_prev; }

    static void on_construct(entt::registry& reg, entt::entity e);
    static void on_destroy(entt::registry& reg, entt::entity e);

private:
    friend struct HierarchyAccess;

    Transform m_local;
    Transform m_world;
    entt::entity m_parent = entt::null;
    entt::entity m_firstChild = entt::null;
    entt::entity m_next = entt::null;
    entt::entity m_prev = entt::null;
};

bool isAffine(const glm::mat4& m);

}
