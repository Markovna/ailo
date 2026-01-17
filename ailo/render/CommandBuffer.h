#pragma once
#include <vector>
#include <vulkan/vulkan.hpp>

#include "UniqueVkHandle.h"

namespace ailo {

class FenceStatus {
public:
    void setSignaled() { m_signaled = true; }
    bool isSignaled() const { return m_signaled; }

private:
    bool m_signaled = false;
};

class CommandBuffer {
public:
    CommandBuffer(vk::CommandBuffer commandBuffer, vk::Device device) :
        m_commandBuffer(commandBuffer),
        m_device(device),
        m_fence(m_device.createFence(vk::FenceCreateInfo{ vk::FenceCreateFlagBits::eSignaled })),
        m_fenceStatus(std::make_shared<FenceStatus>()) {

    }

    CommandBuffer(const CommandBuffer&) = delete;
    CommandBuffer& operator=(const CommandBuffer&) = delete;

    CommandBuffer(CommandBuffer&&) = default;
    CommandBuffer& operator=(CommandBuffer&&) = default;

    ~CommandBuffer() {
        m_device.destroyFence(m_fence);

        if (m_fenceStatus) {
            m_fenceStatus->setSignaled();
        }
    }

    vk::CommandBuffer& operator*() { return m_commandBuffer; }
    vk::CommandBuffer* operator->() { return &m_commandBuffer; }

    void begin() {
        vk::CommandBufferBeginInfo beginInfo{};
        m_commandBuffer.begin(beginInfo);

        m_fenceStatus = std::make_shared<FenceStatus>();
    }

    vk::CommandBuffer& buffer() { return m_commandBuffer; }

    void submit(vk::Queue& queue, vk::Semaphore& signalSemaphore) const;

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
    decltype(auto) getFenceStatusShared() { return m_fenceStatus; }

private:
    vk::CommandBuffer m_commandBuffer;
    vk::Device m_device;
    vk::Fence m_fence;
    std::shared_ptr<FenceStatus> m_fenceStatus;
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

private:
    std::vector<CommandBuffer> m_commandBuffers;
    uint8_t m_currentBufferIndex = 0;
    bool m_recording = false;
};

}
