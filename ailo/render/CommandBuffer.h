#pragma once
#include <vector>
#include <vulkan/vulkan.hpp>

#include "UniqueVkHandle.h"

namespace ailo {

class CommandBuffer {
public:
    CommandBuffer(vk::CommandBuffer commandBuffer, vk::Device device) :
        m_commandBuffer(commandBuffer),
        m_device(device),
        m_fence(m_device.createFence(vk::FenceCreateInfo{ vk::FenceCreateFlagBits::eSignaled })) {

    }

    CommandBuffer(const CommandBuffer&) = delete;
    CommandBuffer& operator=(const CommandBuffer&) = delete;

    CommandBuffer(CommandBuffer&&) = default;
    CommandBuffer& operator=(CommandBuffer&&) = default;

    ~CommandBuffer() {
        m_device.destroyFence(m_fence);
    }

    vk::CommandBuffer& operator*() { return m_commandBuffer; }
    vk::CommandBuffer* operator->() { return &m_commandBuffer; }

    void begin(uint64_t serial) {
        vk::CommandBufferBeginInfo beginInfo{};
        m_commandBuffer.begin(beginInfo);

        m_serial = serial;
        m_submitted = false;
    }

    vk::CommandBuffer& buffer() { return m_commandBuffer; }

    void submit(vk::Queue& queue, vk::Semaphore& signalSemaphore);

    void addWait(vk::Semaphore waitSemaphore, vk::PipelineStageFlags waitStageMask) {
        m_waitSemaphores.push_back(waitSemaphore);
        m_waitStages.push_back(waitStageMask);
    }

    void setSubmitSignal(UniqueVkHandle<vk::Semaphore> semaphore) {
        m_submitSemaphore = std::move(semaphore);

        addWait(m_submitSemaphore.get(), vk::PipelineStageFlagBits::eColorAttachmentOutput);
    }

    void wait();

    void reset();

    vk::Fence& getFence() { return m_fence; }

    // Serial of the last recording started on this buffer (0 if never used).
    uint64_t serial() const { return m_serial; }
    // True unless the buffer was submitted and the GPU has not finished executing it yet.
    bool isComplete() const;

private:
    vk::CommandBuffer m_commandBuffer;
    vk::Device m_device;
    vk::Fence m_fence;
    uint64_t m_serial = 0;
    bool m_submitted = false;
    UniqueVkHandle<vk::Semaphore> m_submitSemaphore;
    std::vector<vk::Semaphore> m_waitSemaphores;
    std::vector<vk::PipelineStageFlags> m_waitStages;
};

class CommandsPool {
public:
    CommandsPool(vk::Device device, vk::CommandPool commandPool);

    CommandBuffer& get();

    void next();
    void destroy();

    uint64_t currentSerial() const { return m_recording ? m_nextSerial : m_nextSerial - 1; }

    // Highest serial whose command buffer, and all before it, finished executing on the GPU.
    uint64_t completedSerial();

private:
    std::vector<CommandBuffer> m_commandBuffers;
    uint8_t m_currentBufferIndex = 0;
    bool m_recording = false;
    uint64_t m_nextSerial = 1;
    uint64_t m_completedSerial = 0;
};

}
