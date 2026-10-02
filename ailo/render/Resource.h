#pragma once

#include <cassert>
#include <concepts>
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

// Type-erased part of ResourceContainer: what a reference needs to hand a resource back once it's the last one.
class ResourceContainerBase {
public:
    virtual void release(uint64_t handleId) = 0;

protected:
    ~ResourceContainerBase() = default;
};

class Resource {
public:
    Resource() noexcept = default;

    Resource(const Resource&) = delete;
    Resource& operator=(const Resource&) = delete;

    [[nodiscard]] bool isDestroyed() const noexcept { return m_destroyed; }
    [[nodiscard]] uint32_t useCount() const noexcept { return m_refCount; }

private:
    template<typename T> friend class ResourceRef;

    void addRef() noexcept {
        assert(!m_destroyed);
        ++m_refCount;
    }

    // Returns true when that was the last reference; the resource is then marked destroyed.
    [[nodiscard]] bool removeRef() noexcept {
        assert(m_refCount > 0);
        if (--m_refCount == 0) {
            m_destroyed = true;
            return true;
        }
        return false;
    }

    uint32_t m_refCount = 0;
    bool m_destroyed = false;
};

// Common part of Unique and Shared: one counted reference to a resource living in a ResourceContainer.
// T may be a base of the stored type (e.g. Shared<Resource>), which lets references to different resource types
// share one container.
template<typename T>
class ResourceRef {
public:
    [[nodiscard]] Handle<T> getHandle() const noexcept { return m_handle; }
    operator Handle<T>() const noexcept { return m_handle; }
    explicit operator bool() const noexcept { return m_ptr != nullptr; }

    T* get() const noexcept { return static_cast<T*>(m_ptr); }
    T& operator*() const noexcept { assert(m_ptr); return *get(); }
    T* operator->() const noexcept { assert(m_ptr); return get(); }

    void reset() noexcept {
        if (m_ptr && m_ptr->removeRef()) {
            m_container->release(m_handle.getId());
        }
        m_container = nullptr;
        m_ptr = nullptr;
        m_handle = {};
    }

    ResourceRef() noexcept = default;

    ResourceRef(const ResourceRef&) = delete;
    ResourceRef& operator=(const ResourceRef&) = delete;

    ~ResourceRef() { reset(); }

protected:
    template<typename U> friend class ResourceRef;


    ResourceRef(ResourceContainerBase* container, Handle<T> handle, T* ptr) noexcept
        : m_container(container), m_ptr(ptr), m_handle(handle) {
        if (m_ptr) {
            m_ptr->addRef();
        }
    }

    // Adds a reference to what rhs refers to. Expects this to be empty.
    template<typename D>
    void share(const ResourceRef<D>& rhs) noexcept {
        m_container = rhs.m_container;
        m_ptr = rhs.m_ptr;
        m_handle = Handle<T>(rhs.m_handle);
        if (m_ptr) {
            m_ptr->addRef();
        }
    }

    // Takes over rhs's reference without touching the count. Expects this to be empty.
    template<typename D>
    void steal(ResourceRef<D>& rhs) noexcept {
        m_container = std::exchange(rhs.m_container, nullptr);
        m_ptr = std::exchange(rhs.m_ptr, nullptr);
        m_handle = Handle<T>(std::exchange(rhs.m_handle, {}));
    }

    ResourceContainerBase* m_container = nullptr;
    Resource* m_ptr = nullptr;
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
    Unique(ResourceContainerBase* container, Handle<T> handle, T* ptr) noexcept
        : ResourceRef<T>(container, handle, ptr) {}

    friend class ResourceContainer<T>;
};

// Copyable ref-counted reference. Converts from Unique/Shared of a derived type, like std::shared_ptr.
template<typename T>
class Shared : public ResourceRef<T> {
public:
    Shared() noexcept = default;

    Shared(const Shared& rhs) noexcept { this->share(rhs); }

    Shared(Shared&& rhs) noexcept { this->steal(rhs); }

    template<typename D> requires std::derived_from<D, T>
    Shared(const Shared<D>& rhs) noexcept { this->share(rhs); }

    template<typename D> requires std::derived_from<D, T>
    Shared(Shared<D>&& rhs) noexcept { this->steal(rhs); }

    template<typename D> requires std::derived_from<D, T>
    Shared(Unique<D>&& unique) noexcept { this->steal(unique); }

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
    Shared(ResourceContainerBase* container, Handle<T> handle, T* ptr) noexcept
        : ResourceRef<T>(container, handle, ptr) {}

    friend class ResourceContainer<T>;
};

}
