#pragma once

#include <stdexcept>

#include "common/slot_map.h"
#include "ResourcePtr.h"

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
        auto key = m_resources.emplace(std::forward<Args>(args)...);
        auto ptr  = m_resources.get(key);
        return { Handle { key.raw }, *ptr };
    }

    void erase(Handle handle) {
        using key_type = typename dod::slot_map<ResourceType>::key;
        m_resources.erase(key_type {handle.getId()});
    }

    reference get(Handle handle) {
        using key_type = typename dod::slot_map<ResourceType>::key;
        auto ptr = m_resources.get(key_type {handle.getId()});
        if (ptr == nullptr) {
            throw std::runtime_error("Resource not found");
        }
        return *ptr;
    }

private:
    dod::slot_map<ResourceType> m_resources {};
};

// Implementation of resource_ptr::make (must be after ResourceContainer definition)
template<typename T>
template<typename ...Args>
resource_ptr<T> resource_ptr<T>::make(ResourceContainer<T>& container, Args&& ...args) {
    auto [handle, ref] = container.emplace(std::forward<Args>(args)...);
    ref.m_container = &container;
    ref.m_handle = handle;
    return resource_ptr { &ref };
}

}
