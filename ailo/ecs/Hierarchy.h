#pragma once

#include <entt/entity/registry.hpp>

#include "Transform.h"

namespace ailo::hierarchy {

enum class Keep { World, Local };

void setLocal(entt::registry& reg, entt::entity e, const Transform& local);
void setWorld(entt::registry& reg, entt::entity e, const Transform& world);
bool setParent(entt::registry& reg, entt::entity child, entt::entity parent, Keep keep = Keep::World);
bool isInSubtree(const entt::registry& reg, entt::entity e, entt::entity root);
void destroySubtree(entt::registry& reg, entt::entity root);

template<typename F>
void forEachChild(const entt::registry& reg, entt::entity e, F&& f) {
    for (auto child = reg.get<TransformComponent>(e).firstChild(); child != entt::null;) {
        const auto next = reg.get<TransformComponent>(child).nextSibling();
        f(child);
        child = next;
    }
}

template<typename F>
void forEachDescendant(const entt::registry& reg, entt::entity root, F&& f) {
    auto node = reg.get<TransformComponent>(root).firstChild();
    while (node != entt::null) {
        f(node);
        if (const auto child = reg.get<TransformComponent>(node).firstChild(); child != entt::null) {
            node = child;
            continue;
        }
        // Climb until a node has a next sibling, stopping at the root.
        while (node != root) {
            const auto& tc = reg.get<TransformComponent>(node);
            if (tc.nextSibling() != entt::null) {
                node = tc.nextSibling();
                break;
            }
            node = tc.parent();
        }
        if (node == root) node = entt::null;
    }
}

}
