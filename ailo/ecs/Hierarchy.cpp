#include "Hierarchy.h"

#include <cassert>
#include <vector>

namespace ailo {

struct HierarchyAccess {
    static TransformComponent& get(entt::registry& reg, entt::entity e) { return reg.get<TransformComponent>(e); }

    static const Transform* parentWorld(entt::registry& reg, const TransformComponent& tc) {
        return tc.m_parent != entt::null ? &get(reg, tc.m_parent).m_world : nullptr;
    }

    // Recomputes the world transform of every descendant of `root` from its (already up to date) world transform.
    static void propagate(entt::registry& reg, entt::entity root) {
        hierarchy::forEachDescendant(reg, root, [&](entt::entity e) {
            auto& tc = get(reg, e);
            tc.m_world = compose(get(reg, tc.m_parent).m_world, tc.m_local);
        });
    }

    static void link(entt::registry& reg, entt::entity child, entt::entity parent) {
        auto& c = get(reg, child);
        auto& p = get(reg, parent);
        c.m_parent = parent;
        c.m_next = entt::null;
        c.m_prev = entt::null;
        if (p.m_firstChild == entt::null) {
            p.m_firstChild = child;
            return;
        }
        auto last = p.m_firstChild;
        while (get(reg, last).m_next != entt::null) last = get(reg, last).m_next;
        get(reg, last).m_next = child;
        c.m_prev = last;
    }

    static void unlink(entt::registry& reg, entt::entity child) {
        auto& c = get(reg, child);
        if (c.m_prev != entt::null) {
            get(reg, c.m_prev).m_next = c.m_next;
        } else if (c.m_parent != entt::null) {
            get(reg, c.m_parent).m_firstChild = c.m_next;
        }
        if (c.m_next != entt::null) get(reg, c.m_next).m_prev = c.m_prev;
        c.m_parent = c.m_next = c.m_prev = entt::null;
    }

    static void setLocal(entt::registry& reg, entt::entity e, const Transform& local) {
        auto& tc = get(reg, e);
        tc.m_local = local;
        const Transform* parent = parentWorld(reg, tc);
        tc.m_world = parent ? compose(*parent, local) : local;
        propagate(reg, e);
    }

    static void setWorld(entt::registry& reg, entt::entity e, const Transform& world) {
        auto& tc = get(reg, e);
        tc.m_world = world;
        const Transform* parent = parentWorld(reg, tc);
        tc.m_local = parent ? relativeTo(*parent, world, tc.m_local) : world;
        propagate(reg, e);
    }

    static bool setParent(entt::registry& reg, entt::entity child, entt::entity parent, hierarchy::Keep keep) {
        auto& tc = get(reg, child);
        // A leaf's subtree is just itself, which skips the O(depth) walk for the common case of attaching new nodes.
        if (parent == child) return false;
        if (parent != entt::null && tc.m_firstChild != entt::null && hierarchy::isInSubtree(reg, parent, child)) {
            return false;
        }

        if (tc.m_parent == parent) return true;

        unlink(reg, child);
        if (parent != entt::null) link(reg, child, parent);

        const Transform* pw = parentWorld(reg, tc);
        if (keep == hierarchy::Keep::World) {
            // The world transform doesn't change, so neither does any descendant's.
            tc.m_local = pw ? relativeTo(*pw, tc.m_world, tc.m_local) : tc.m_world;
        } else {
            tc.m_world = pw ? compose(*pw, tc.m_local) : tc.m_local;
            propagate(reg, child);
        }
        return true;
    }

    static void onConstruct(entt::registry& reg, entt::entity e) {
        // Links can only be made by setParent; a copied component would corrupt both trees.
        [[maybe_unused]] const auto& tc = get(reg, e);
        assert(tc.m_parent == entt::null && tc.m_firstChild == entt::null);
        assert(tc.m_next == entt::null && tc.m_prev == entt::null);
    }

    // Runs while every component still exists, also for each entity during registry.clear().
    // Must not destroy anything itself.
    static void onDestroy(entt::registry& reg, entt::entity e) {
        auto& tc = get(reg, e);
        for (auto child = tc.m_firstChild; child != entt::null;) {
            auto& c = get(reg, child);
            const auto next = c.m_next;
            c.m_parent = c.m_next = c.m_prev = entt::null;
            c.m_local = c.m_world;
            child = next;
        }
        tc.m_firstChild = entt::null;
        unlink(reg, e);
    }
};

void TransformComponent::on_construct(entt::registry& reg, entt::entity e) {
    HierarchyAccess::onConstruct(reg, e);
}

void TransformComponent::on_destroy(entt::registry& reg, entt::entity e) {
    HierarchyAccess::onDestroy(reg, e);
}

namespace hierarchy {

void setLocal(entt::registry& reg, entt::entity e, const Transform& local) {
    HierarchyAccess::setLocal(reg, e, local);
}

void setWorld(entt::registry& reg, entt::entity e, const Transform& world) {
    HierarchyAccess::setWorld(reg, e, world);
}

bool setParent(entt::registry& reg, entt::entity child, entt::entity parent, Keep keep) {
    return HierarchyAccess::setParent(reg, child, parent, keep);
}

bool isInSubtree(const entt::registry& reg, entt::entity e, entt::entity root) {
    for (; e != entt::null; e = reg.get<TransformComponent>(e).parent()) {
        if (e == root) return true;
    }
    return false;
}

void destroySubtree(entt::registry& reg, entt::entity root) {
    std::vector<entt::entity> nodes { root };
    forEachDescendant(reg, root, [&](entt::entity e) { nodes.push_back(e); });
    // Children first, so no node is orphaned on the way.
    for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) reg.destroy(*it);
}

}

}
