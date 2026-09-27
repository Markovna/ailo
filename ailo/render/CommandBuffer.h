#pragma once
#include <vector>
#include <vulkan/vulkan.hpp>

#include "Resource.h"

namespace ailo {

class CommandBuffer {
public:
    CommandBuffer(vk::CommandBuffer commandBuffer, vk::Semaphore timeline) :
        m_commandBuffer(commandBuffer),
        m_timeline(timeline) {

    }

    CommandBuffer(const CommandBuffer&) = delete;
    CommandBuffer& operator=(const CommandBuffer&) = delete;

    CommandBuffer(CommandBuffer&&) = default;
    CommandBuffer& operator=(CommandBuffer&&) = default;

    vk::CommandBuffer& operator*() { return m_commandBuffer; }
    vk::CommandBuffer* operator->() { return &m_commandBuffer; }

    void begin(uint64_t serial) {
        vk::CommandBufferBeginInfo beginInfo{};
        m_commandBuffer.begin(beginInfo);

        m_serial = serial;
        m_submitted = false;
    }

    vk::CommandBuffer& buffer() { return m_commandBuffer; }

    // Signals signalSemaphore and the pool's timeline semaphore with this buffer's serial.
    void submit(vk::Queue& queue, vk::Semaphore& signalSemaphore);

    void addWait(vk::Semaphore waitSemaphore, vk::PipelineStageFlags waitStageMask) {
        m_waitSemaphores.push_back(waitSemaphore);
        m_waitStages.push_back(waitStageMask);
    }

    void setSubmitSignal(vk::UniqueSemaphore semaphore) {
        m_submitSemaphore = std::move(semaphore);

        addWait(m_submitSemaphore.get(), vk::PipelineStageFlagBits::eColorAttachmentOutput);
    }

    void reset();

    void acquire(Shared<Resource> resource) { m_acquired.push_back(std::move(resource)); }

    uint64_t serial() const { return m_serial; }
    bool isSubmitted() const { return m_submitted; }

private:
    vk::CommandBuffer m_commandBuffer;
    vk::Semaphore m_timeline;
    uint64_t m_serial = 0;
    bool m_submitted = false;
    vk::UniqueSemaphore m_submitSemaphore;
    std::vector<vk::Semaphore> m_waitSemaphores;
    std::vector<vk::PipelineStageFlags> m_waitStages;
    std::vector<Shared<Resource>> m_acquired;
};

class CommandsPool {
public:
    CommandsPool(vk::Device device, vk::CommandPool commandPool);

    CommandBuffer& get();

    void next();
    void destroy();

    uint64_t currentSerial() const { return m_recording ? m_nextSerial : m_nextSerial - 1; }

    // Highest serial whose command buffer, and all before it, finished executing on the GPU.
    uint64_t completedSerial() const { return m_device.getSemaphoreCounterValue(m_timeline); }

private:
    void waitForSerial(uint64_t serial) const;

    vk::Device m_device;
    vk::Semaphore m_timeline;
    std::vector<CommandBuffer> m_commandBuffers;
    uint8_t m_currentBufferIndex = 0;
    bool m_recording = false;
    uint64_t m_nextSerial = 1;
};

}
