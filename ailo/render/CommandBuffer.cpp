#include "CommandBuffer.h"

namespace ailo {

void CommandBuffer::submit(vk::Queue& queue, vk::Semaphore& signalSemaphore) const {
    m_commandBuffer.end();

    vk::SubmitInfo submitInfo{};
    submitInfo.waitSemaphoreCount = m_waitSemaphores.size();
    submitInfo.pWaitSemaphores = m_waitSemaphores.data();
    submitInfo.pWaitDstStageMask = m_waitStages.data();
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_commandBuffer;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &signalSemaphore;

    queue.submit(submitInfo, m_fence);
}

CommandsPool::CommandsPool(vk::Device device, vk::CommandPool commandPool) {
    const uint32_t numCommandBuffers = 4;
    vk::CommandBufferAllocateInfo allocInfo{};
    allocInfo.commandPool = commandPool;
    allocInfo.level = vk::CommandBufferLevel::ePrimary;
    allocInfo.commandBufferCount = numCommandBuffers;

    m_commandBuffers.reserve(numCommandBuffers);
    auto commandBuffers = device.allocateCommandBuffers(allocInfo);
    for (auto&& cb : commandBuffers) {
        m_commandBuffers.emplace_back(std::move(cb), device);
    }
}

CommandBuffer& CommandsPool::get() {
    if (m_recording) {
        return m_commandBuffers[m_currentBufferIndex];
    }

    auto& buffer = m_commandBuffers[m_currentBufferIndex];
    buffer.wait();

    buffer.reset();
    buffer.begin();
    m_recording = true;
    return buffer;
}

void CommandsPool::next() {
    m_currentBufferIndex = (m_currentBufferIndex + 1) % m_commandBuffers.size();
    m_recording = false;
}

void CommandsPool::destroy() {
    for (auto& cb : m_commandBuffers) {
        cb.reset();
    }
    m_commandBuffers.clear();
}

void CommandBuffer::wait() {
    (void)m_device.waitForFences(1, &m_fence, VK_TRUE, UINT64_MAX);
    m_fenceStatus->setSignaled();
}

void CommandBuffer::reset() {
    m_waitSemaphores.clear();
    m_waitStages.clear();
    m_commandBuffer.reset();

    (void)m_device.resetFences(1, &m_fence);

    m_fenceStatus->setSignaled();
    m_fenceStatus.reset();

    m_submitSemaphore.reset();
}

}
