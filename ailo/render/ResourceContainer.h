#pragma once

#include <stdexcept>
#include <vector>

#include "common/slot_map.h"
#include "Resource.h"

namespace ailo {

// Stores resources of one type. When the last reference to a resource is dropped it is queued and erased by the next
// collect(). References are held by everything that may still use the resource, including the command buffers that
// recorded commands with it, so a resource with no references is no longer used by the GPU.
template<typename ResourceType>
class ResourceContainer final : public ResourceContainerBase {
    static_assert(std::is_base_of_v<Resource, ResourceType>, "GPU resources must derive from Resource");

public:
    using Handle = Handle<ResourceType>;
    using value_type = ResourceType;
    using reference = value_type&;
    using pointer = value_type*;

    ResourceContainer() = default;

    ResourceContainer(const ResourceContainer&) = delete;
    ResourceContainer& operator=(const ResourceContainer&) = delete;

    template<typename... Args>
    Unique<ResourceType> make(Args&&... args) {
        auto key = m_resources.emplace(std::forward<Args>(args)...);
        return { this, Handle { key.raw }, m_resources.get(key) };
    }

    // Adds a reference to a live resource.
    Shared<ResourceType> share(Handle handle) {
        return { this, handle, &get(handle) };
    }

    reference get(Handle handle) {
        using key_type = typename dod::slot_map<ResourceType>::key;
        auto ptr = m_resources.get(key_type { handle.getId() });
        if (ptr == nullptr) {
            throw std::runtime_error("Resource not found");
        }
        assert(!ptr->isDestroyed() && "Resource is used after its last reference was dropped");
        return *ptr;
    }

    void release(uint64_t handleId) override {
        m_pending.emplace_back(handleId);
    }

    // Erases every resource whose last reference was dropped. Call once per frame.
    void collect() {
        // Index-based: erasing may release more resources, and in principle into this container too.
        for (size_t i = 0; i < m_pending.size(); i++) {
            erase(m_pending[i]);
        }
        m_pending.clear();
    }

    // Destroys every resource, referenced or not. Only valid on shutdown, once all owners are gone.
    void clear() {
        m_pending.clear();
        m_resources.clear();
    }

private:
    void erase(Handle handle) {
        using key_type = typename dod::slot_map<ResourceType>::key;
        m_resources.erase(key_type { handle.getId() });
    }

    dod::slot_map<ResourceType> m_resources {};
    std::vector<Handle> m_pending;
};

}
