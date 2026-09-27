#pragma once

#include <cassert>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>

namespace ailo {

template<typename T> class ResourceContainer;

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

    [[nodiscard]] HandleId getId() const noexcept { return id; }

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

template<typename T> class ResourceRef;

class Resource {
public:
    Resource() noexcept = default;

    Resource(const Resource&) = delete;
    Resource& operator=(const Resource&) = delete;

    [[nodiscard]] bool isDestroyed() const noexcept { return m_destroyed; }
    [[nodiscard]] uint32_t useCount() const noexcept { return m_refCount; }

private:
    template<typename T> friend class ResourceRef;

    uint32_t m_refCount = 0;
    bool m_destroyed = false;
};

template<typename T>
class ResourceRef {
public:
    [[nodiscard]] Handle<T> getHandle() const noexcept { return m_handle; }
    operator Handle<T>() const noexcept { return m_handle; }
    explicit operator bool() const noexcept { return m_ptr != nullptr; }

    T* get() const noexcept { return m_ptr; }
    T& operator*() const noexcept { assert(m_ptr); return *m_ptr; }
    T* operator->() const noexcept { assert(m_ptr); return m_ptr; }

    void reset() noexcept {
        if (m_ptr) {
            Resource& resource = *m_ptr;
            assert(resource.m_refCount > 0);
            if (--resource.m_refCount == 0) {
                resource.m_destroyed = true;
                m_container->release(m_handle);
            }
        }
        m_container = nullptr;
        m_ptr = nullptr;
        m_handle = {};
    }

protected:
    ResourceRef() noexcept = default;

    ResourceRef(ResourceContainer<T>* container, Handle<T> handle, T* ptr) noexcept
        : m_container(container), m_ptr(ptr), m_handle(handle) {
        if (m_ptr) {
            Resource& resource = *m_ptr;
            assert(!resource.m_destroyed);
            ++resource.m_refCount;
        }
    }

    ~ResourceRef() { reset(); }

    // Takes over rhs's reference without touching the count.
    void steal(ResourceRef& rhs) noexcept {
        m_container = std::exchange(rhs.m_container, nullptr);
        m_ptr = std::exchange(rhs.m_ptr, nullptr);
        m_handle = std::exchange(rhs.m_handle, {});
    }

    ResourceContainer<T>* m_container = nullptr;
    T* m_ptr = nullptr;
    Handle<T> m_handle {};
};

// Move-only owning reference. The backend may still take internal Shared references (e.g. a render target
// referencing its textures), so the resource can outlive the Unique, but never the last reference.
template<typename T>
class Unique : public ResourceRef<T> {
public:
    Unique() noexcept = default;

    Unique(Unique&& rhs) noexcept { this->steal(rhs); }

    Unique& operator=(Unique&& rhs) noexcept {
        if (this != &rhs) {
            this->reset();
            this->steal(rhs);
        }
        return *this;
    }

    Unique(const Unique&) = delete;
    Unique& operator=(const Unique&) = delete;

private:
    Unique(ResourceContainer<T>* container, Handle<T> handle, T* ptr) noexcept
        : ResourceRef<T>(container, handle, ptr) {}

    friend class ResourceContainer<T>;
};

// Copyable ref-counted reference.
template<typename T>
class Shared : public ResourceRef<T> {
public:
    Shared() noexcept = default;

    Shared(Unique<T>&& unique) noexcept { this->steal(unique); }

    Shared(const Shared& rhs) noexcept : ResourceRef<T>(rhs.m_container, rhs.m_handle, rhs.m_ptr) {}

    Shared(Shared&& rhs) noexcept { this->steal(rhs); }

    Shared& operator=(const Shared& rhs) noexcept {
        if (this != &rhs) {
            Shared copy { rhs };
            *this = std::move(copy);
        }
        return *this;
    }

    Shared& operator=(Shared&& rhs) noexcept {
        if (this != &rhs) {
            this->reset();
            this->steal(rhs);
        }
        return *this;
    }

private:
    Shared(ResourceContainer<T>* container, Handle<T> handle, T* ptr) noexcept
        : ResourceRef<T>(container, handle, ptr) {}

    friend class ResourceContainer<T>;
};

}
