#pragma once

#include <stdexcept>
#include <vector>

#include "common/slot_map.h"
#include "Resource.h"
#include "CommandBuffer.h"

namespace ailo {

template<typename ResourceType>
class ResourceContainer {
    static_assert(std::is_base_of_v<Resource, ResourceType>, "GPU resources must derive from Resource");

public:
    using Handle = Handle<ResourceType>;
    using value_type = ResourceType;
    using reference = value_type&;
    using pointer = value_type*;

    explicit ResourceContainer(const CommandsPool& commands) : m_commands(commands) {}

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

    void release(Handle handle) {
        m_pending.push_back({ m_commands.currentSerial(), handle });
    }

    // Erases released resources whose command buffers have completed. Call once per frame.
    void collect(uint64_t completedSerial) {
        // Index-based: destructors may release resources into other containers, never into this one,
        // but don't rely on references into m_pending staying valid.
        size_t count = 0;
        while (count < m_pending.size() && m_pending[count].serial <= completedSerial) {
            erase(m_pending[count].handle);
            ++count;
        }
        m_pending.erase(m_pending.begin(), m_pending.begin() + static_cast<ptrdiff_t>(count));
    }

    // Erases every released resource regardless of GPU progress. Only valid once the device is idle.
    void flush() {
        collect(std::numeric_limits<uint64_t>::max());
    }

    // Destroys every resource, referenced or not. Only valid on shutdown, once all owners are gone.
    void clear() {
        m_pending.clear();
        m_resources.clear();
    }

private:
    struct PendingRelease {
        uint64_t serial;
        Handle handle;
    };

    void erase(Handle handle) {
        using key_type = typename dod::slot_map<ResourceType>::key;
        m_resources.erase(key_type { handle.getId() });
    }

    const CommandsPool& m_commands;
    dod::slot_map<ResourceType> m_resources {};
    std::vector<PendingRelease> m_pending;
};

}
