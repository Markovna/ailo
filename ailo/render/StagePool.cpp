#include "StagePool.h"

#include <algorithm>
#include <cassert>
#include <stdexcept>
#include <utility>

#include "CommandBuffer.h"

namespace ailo {

namespace {

vk::DeviceSize alignUp(vk::DeviceSize value, vk::DeviceSize alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

}

StagePool::~StagePool() {
    assert(!m_current.buffer && m_pending.empty() && m_free.empty() && "call destroy() once the device is idle");
}

StagePool::Allocation StagePool::allocate(vk::DeviceSize size, vk::DeviceSize alignment) {
    assert(size > 0 && alignment > 0);

    vk::DeviceSize offset = alignUp(m_current.head, alignment);
    if (!m_current.buffer || offset + size > m_current.capacity) {
        if (m_current.buffer) {
            m_pending.push_back(std::exchange(m_current, {}));
        }
        m_current = acquireBlock(size);
        offset = 0;
    }

    m_current.head = offset + size;
    m_current.lastSerial = m_commands.currentSerial();

    return {
        .buffer = m_current.buffer,
        .offset = offset,
        .mapping = m_current.mapping + offset,
        .allocation = m_current.allocation,
    };
}

void StagePool::flush(const Allocation& allocation, vk::DeviceSize size) const {
    vmaFlushAllocation(m_allocator, allocation.allocation, allocation.offset, size);
}

void StagePool::collect() {
    // Also keeps collect() away from the timeline semaphore once destroy() has run at shutdown.
    if (!m_current.buffer && m_pending.empty() && m_free.empty()) {
        return;
    }

    const uint64_t completed = m_commands.completedSerial();

    // Nothing in flight copies from the current block any more: start filling it from the beginning again.
    if (m_current.buffer && m_current.lastSerial <= completed) {
        m_current.head = 0;
    }

    for (auto it = m_pending.begin(); it != m_pending.end();) {
        if (it->lastSerial > completed) {
            ++it;
            continue;
        }

        // A burst of uploads (e.g. loading a scene) can retire many blocks at once; keep only a few for reuse.
        if (it->capacity > kBlockSize || m_free.size() >= kMaxFreeBlocks) {
            destroyBlock(*it);
        } else {
            it->head = 0;
            it->idleFrames = 0;
            m_free.push_back(*it);
        }
        it = m_pending.erase(it);
    }

    std::erase_if(m_free, [this](Block& block) {
        if (++block.idleFrames <= kIdleFramesBeforeRelease) {
            return false;
        }
        destroyBlock(block);
        return true;
    });
}

void StagePool::destroy() {
    if (m_current.buffer) {
        destroyBlock(m_current);
        m_current = {};
    }
    for (auto& block : m_pending) {
        destroyBlock(block);
    }
    for (auto& block : m_free) {
        destroyBlock(block);
    }
    m_pending.clear();
    m_free.clear();
}

StagePool::Block StagePool::acquireBlock(vk::DeviceSize minCapacity) {
    auto it = std::ranges::find_if(m_free, [minCapacity](const Block& block) { return block.capacity >= minCapacity; });
    if (it != m_free.end()) {
        Block block = *it;
        m_free.erase(it);
        block.idleFrames = 0;
        return block;
    }

    const vk::DeviceSize capacity = std::max(minCapacity, kBlockSize);
    VkBufferCreateInfo const bufferInfo {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = capacity,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
    };
    VmaAllocationCreateInfo const allocInfo {
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
    };

    VkBuffer buffer;
    Block block {};
    VmaAllocationInfo info {};
    if (vmaCreateBuffer(m_allocator, &bufferInfo, &allocInfo, &buffer, &block.allocation, &info) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate a staging block");
    }

    block.buffer = buffer;
    block.mapping = static_cast<std::byte*>(info.pMappedData);
    block.capacity = capacity;
    return block;
}

void StagePool::destroyBlock(Block& block) {
    vmaDestroyBuffer(m_allocator, block.buffer, block.allocation);
}

}
