#pragma once

#include "common/slot_map.h"

namespace ailo {

template<typename T>
class Handle {
public:
    using HandleId = uint64_t;

    Handle() noexcept = default;

    Handle(const Handle&) noexcept = default;
    Handle& operator=(const Handle&) noexcept = default;

    Handle(Handle&& rhs) noexcept
        : id(rhs.id) {
        rhs.id = invalid;
    }

    explicit Handle(HandleId id) noexcept: id(id) {}

    Handle& operator=(Handle&& rhs) noexcept {
        if (this != &rhs) {
            id = rhs.id;
            rhs.id = invalid;
        }
        return *this;
    }

    bool operator==(Handle other) const noexcept { return id == other.id; }
    bool operator!=(Handle other) const noexcept { return id != other.id; }

    explicit constexpr operator bool() const noexcept { return id != invalid; }

    template<typename D, typename = std::enable_if_t<std::is_base_of<T, D>::value> >
    Handle(const Handle<D>& derived) noexcept : Handle(derived.id) {}

    HandleId getId() const noexcept { return id; }

    template<typename B>
    static std::enable_if_t<std::is_base_of_v<B, T>, Handle>
    cast(Handle<B>& from) {
        return Handle(from.getId());
    }

private:
    static constexpr HandleId invalid = HandleId { std::numeric_limits<uint32_t>::max() };

    template<class U> friend
    class Handle;

private:
    HandleId id = invalid;
};

template<typename ResourceType>
class ResourceContainer {
public:
    using Handle = Handle<ResourceType>;
    using value_type = ResourceType;
    using reference = value_type&;
    using const_reference = const value_type&;
    using pointer = value_type*;
    using const_pointer = const value_type*;

    using key = dod::slot_map<ResourceType>::key;

    template<typename...Args>
    std::pair<Handle, reference> emplace(Args&&... args) {
        auto key = m_resources.emplace(std::forward<Args>(args)...);
        auto ptr  = m_resources.get(key);
        return { Handle { key.raw }, *ptr };
    }

    void erase(Handle handle) {

        m_resources.erase(key {handle.getId()});
    }

    reference get(Handle handle) { return *m_resources.get(key {handle.getId()}); }

private:
    dod::slot_map<ResourceType> m_resources;
};

template<typename T>
class resource_ptr {
public:
    template<typename ...Args>
    static resource_ptr make(ResourceContainer<T>& container, Args&& ...args) {
        auto [handle, ref] = container.emplace(std::forward<Args>(args)...);
        ref.m_ptr = resource_ptr { &ref };
        ref.m_container = &container;
        ref.m_handle = handle;
        return ref.m_ptr;
    }

private:
    resource_ptr(T* ptr) : m_ptr(ptr) {
        inc();
    }

public:
    resource_ptr() noexcept = default;

    resource_ptr(const resource_ptr& rhs) noexcept {
        *this = rhs;
    }

    resource_ptr(resource_ptr&& rhs) noexcept {
        *this = std::move(rhs);
    }

    resource_ptr& operator=(const resource_ptr& rhs) noexcept {
        if (this == &rhs) { return *this; }

        dec();
        m_ptr = rhs.m_ptr;
        inc();
        return *this;
    }

    resource_ptr& operator=(resource_ptr&& rhs) noexcept {
        std::swap(m_ptr, rhs.m_ptr);
        return *this;
    }

    ~resource_ptr() {
        dec();
    }

    void reset() {
        dec();
        m_ptr = nullptr;
    }

    Handle<T> getHandle() const noexcept { return m_ptr->m_handle; }

private:
    void dec() {
        if (m_ptr && --m_ptr->m_refCount == 0) {
            destroy(m_ptr);
        }
    }

    void inc() {
        if (m_ptr) { ++m_ptr->m_refCount; }
    }

    static void destroy(T* ptr) {
        ptr->m_container->erase(ptr->m_handle);
    }

    T* m_ptr = nullptr;
};

template<typename T>
class enable_resource_ptr {
private:
    uint32_t m_refCount = 0;
    ResourceContainer<T>* m_container {};
    resource_ptr<T> m_ptr {};
    Handle<T> m_handle {};

public:
    void release() { m_ptr.reset(); }
    resource_ptr<T> getSharedPtr() { return m_ptr; }

    friend class resource_ptr<T>;
};

}
