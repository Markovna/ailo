#pragma once

#include <vulkan/vulkan.hpp>

namespace ailo {

class SemaphoreQueue {
public:
    void init(vk::Device device, uint32_t capacity);
    void destroy(vk::Device device);
    void moveNext();
    vk::Semaphore& get();

private:
    std::vector<vk::Semaphore> m_semaphores;
    uint8_t m_currentIndex = 0;
};

}