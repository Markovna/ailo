#include "CommandBuffer.h"

#include <array>
#include <cassert>

namespace ailo {

void CommandBuffer::submit(vk::Queue& queue, vk::Semaphore& signalSemaphore) {
    m_commandBuffer.end();

    std::array signalSemaphores{ signalSemaphore, m_timeline };
    std::array<uint64_t, 2> signalValues{ 0, m_serial };

    vk::TimelineSemaphoreSubmitInfo timelineInfo{};
    timelineInfo.signalSemaphoreValueCount = signalValues.size();
    timelineInfo.pSignalSemaphoreValues = signalValues.data();

    vk::SubmitInfo submitInfo{};
    submitInfo.pNext = &timelineInfo;
    submitInfo.waitSemaphoreCount = m_waitSemaphores.size();
    submitInfo.pWaitSemaphores = m_waitSemaphores.data();
    submitInfo.pWaitDstStageMask = m_waitStages.data();
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_commandBuffer;
    submitInfo.signalSemaphoreCount = signalSemaphores.size();
    submitInfo.pSignalSemaphores = signalSemaphores.data();

    queue.submit(submitInfo);
    m_submitted = true;
}

CommandsPool::CommandsPool(vk::Device device, vk::CommandPool commandPool) :
    m_device(device) {
    vk::SemaphoreTypeCreateInfo timelineInfo{ vk::SemaphoreType::eTimeline, 0 };
    vk::SemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.pNext = &timelineInfo;
    m_timeline = device.createSemaphore(semaphoreInfo);

    const uint32_t numCommandBuffers = 10;
    vk::CommandBufferAllocateInfo allocInfo{};
    allocInfo.commandPool = commandPool;
    allocInfo.level = vk::CommandBufferLevel::ePrimary;
    allocInfo.commandBufferCount = numCommandBuffers;

    m_commandBuffers.reserve(numCommandBuffers);
    auto commandBuffers = device.allocateCommandBuffers(allocInfo);
    for (auto&& cb : commandBuffers) {
        m_commandBuffers.emplace_back(std::move(cb), m_timeline);
    }
}

CommandBuffer& CommandsPool::get() {
    if (m_recording) {
        return m_commandBuffers[m_currentBufferIndex];
    }

    auto& buffer = m_commandBuffers[m_currentBufferIndex];
    waitForSerial(buffer.serial());

    buffer.reset();
    buffer.begin(m_nextSerial);
    m_recording = true;
    return buffer;
}

void CommandsPool::next() {
    if (m_recording) {
        // An unsubmitted serial would never be signalled, stalling completedSerial() forever.
        assert(m_commandBuffers[m_currentBufferIndex].isSubmitted());
        m_nextSerial++;
    }
    m_currentBufferIndex = (m_currentBufferIndex + 1) % m_commandBuffers.size();
    m_recording = false;
}

void CommandsPool::waitForSerial(uint64_t serial) const {
    vk::SemaphoreWaitInfo waitInfo{};
    waitInfo.semaphoreCount = 1;
    waitInfo.pSemaphores = &m_timeline;
    waitInfo.pValues = &serial;
    (void)m_device.waitSemaphores(waitInfo, UINT64_MAX);
}

void CommandsPool::destroy() {
    for (auto& cb : m_commandBuffers) {
        cb.reset();
    }
    m_commandBuffers.clear();

    m_device.destroySemaphore(m_timeline);
    m_timeline = nullptr;
}

void CommandBuffer::reset() {
    m_waitSemaphores.clear();
    m_waitStages.clear();
    m_commandBuffer.reset();
    m_submitted = false;

    m_submitSemaphore.reset();
    m_acquired.clear();
}

}
