#include "CommandBuffer.h"

namespace ailo {

void CommandBuffer::submit(vk::Queue& queue, vk::Semaphore& signalSemaphore) {
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
    m_submitted = true;
}

bool CommandBuffer::isComplete() const {
    return !m_submitted || m_device.getFenceStatus(m_fence) == vk::Result::eSuccess;
}

CommandsPool::CommandsPool(vk::Device device, vk::CommandPool commandPool) {
    const uint32_t numCommandBuffers = 10;
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
    buffer.begin(m_nextSerial);
    m_recording = true;
    return buffer;
}

void CommandsPool::next() {
    if (m_recording) {
        m_nextSerial++;
    }
    m_currentBufferIndex = (m_currentBufferIndex + 1) % m_commandBuffers.size();
    m_recording = false;
}

uint64_t CommandsPool::completedSerial() {
    // TODO: replace with Timeline semaphores
    // Fences may be observed signalled out of order, so the result is capped just below
    // the oldest submitted buffer that is still pending.
    uint64_t completed = m_nextSerial - 1;
    for (const auto& cb : m_commandBuffers) {
        const uint64_t serial = cb.serial();
        if (serial > m_completedSerial && serial < m_nextSerial && !cb.isComplete()) {
            completed = std::min(completed, serial - 1);
        }
    }

    m_completedSerial = std::max(m_completedSerial, completed);
    return m_completedSerial;
}

void CommandsPool::destroy() {
    for (auto& cb : m_commandBuffers) {
        cb.reset();
    }
    m_commandBuffers.clear();
}

void CommandBuffer::wait() {
    (void)m_device.waitForFences(1, &m_fence, VK_TRUE, UINT64_MAX);
}

void CommandBuffer::reset() {
    m_waitSemaphores.clear();
    m_waitStages.clear();
    m_commandBuffer.reset();

    (void)m_device.resetFences(1, &m_fence);
    m_submitted = false;

    m_submitSemaphore.reset();
}

}
