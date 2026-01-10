#pragma once
#include <vulkan/vulkan.hpp>

namespace ailo {

template<class T>
class HandleDeleter;

template<>
class HandleDeleter<vk::Semaphore> {
public:
    void operator()(vk::Device device, vk::Semaphore semaphore) const { device.destroySemaphore(semaphore); }
};


template<typename T>
class UniqueVkHandle {
public:
    UniqueVkHandle() = default;
    UniqueVkHandle(vk::Device device, T handle) : m_device(device), m_handle(handle) {}

    UniqueVkHandle(UniqueVkHandle&) = delete;
    UniqueVkHandle& operator=(UniqueVkHandle&) = delete;

    UniqueVkHandle(UniqueVkHandle&& other) noexcept
        : m_device(std::move(other.m_device)), m_handle(std::move(other.m_handle)) {
        other.m_handle = nullptr;
    }

    UniqueVkHandle& operator=(UniqueVkHandle&& other) noexcept {
        if (&other != this) {
            m_device = std::move(other.m_device);
            m_handle = std::move(other.m_handle);

            other.m_handle = nullptr;
        }

        return *this;
    }

    ~UniqueVkHandle() {
        if (m_handle) {
            HandleDeleter<T> deleter;
            deleter(m_device, m_handle);
        }
    }

    T& get() { return m_handle; }
    void reset() {
        if (m_handle) {
            HandleDeleter<T> deleter;
            deleter(m_device, m_handle);
        }

        m_handle = nullptr;
    }

private:
    vk::Device m_device = nullptr;
    T m_handle = nullptr;

};

}
