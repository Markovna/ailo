//
// Created by abdus on 2026-01-08.
//

#include "SemaphoreQueue.h"

#include "RenderAPI.h"


void ailo::SemaphoreQueue::destroy(vk::Device device) {
    for (auto& semaphore : m_semaphores) {
        device.destroySemaphore(semaphore);
    }
}

void ailo::SemaphoreQueue::moveNext() {
    m_currentIndex = (m_currentIndex + 1) % m_semaphores.size();
}

vk::Semaphore& ailo::SemaphoreQueue::get() {
    return m_semaphores[m_currentIndex];
}

void ailo::SemaphoreQueue::init(vk::Device device, uint32_t capacity) {
    vk::SemaphoreCreateInfo semaphoreInfo {};

    for (int i = 0; i < capacity; i++) {
        m_semaphores.emplace_back(device.createSemaphore(semaphoreInfo));
    }
}
