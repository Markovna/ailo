#pragma once

#include "common/SparseSet.h"
#include "utils/ResourceAllocator.h"

namespace ailo {

template<typename ResourceType>
class ResourceContainer {
public:
    using Handle = Handle<ResourceType>;
    using value_type = ResourceType;
    using reference = value_type&;
    using const_reference = const value_type&;
    using pointer = value_type*;
    using const_pointer = const value_type*;

    template<typename...Args>
    std::pair<Handle, reference> emplace(Args&&... args) {
        auto [key, ref] = m_resources.emplace(std::forward<Args>(args)...);
        return { Handle(key), ref };
    }

    void erase(Handle handle) { m_resources.erase(handle.getId()); }

    reference get(Handle handle) { return m_resources[handle.getId()]; }

private:
    SparseSet<uint32_t, ResourceType> m_resources;
};

}
