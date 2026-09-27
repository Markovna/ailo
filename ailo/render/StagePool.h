#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>

namespace ailo {

class CommandsPool;

// Host-visible staging memory for CPU -> GPU uploads. Allocations are carved linearly out of large persistently
// mapped blocks, so an upload costs a memcpy instead of creating, mapping and destroying a buffer. A block is reused
// once the GPU has completed every command buffer that copies from it.
class StagePool {
public:
    struct Allocation {
        vk::Buffer buffer;
        vk::DeviceSize offset = 0;
        void* mapping = nullptr; // CPU address of `offset`
        VmaAllocation allocation = nullptr;
    };

    StagePool(VmaAllocator allocator, CommandsPool& commands) : m_allocator(allocator), m_commands(commands) {}
    ~StagePool();

    StagePool(const StagePool&) = delete;
    StagePool& operator=(const StagePool&) = delete;

    // `size` bytes whose offset is a multiple of `alignment`. The memory stays valid until the command buffer being
    // recorded completes, so the copy reading from it must be recorded into that command buffer.
    Allocation allocate(vk::DeviceSize size, vk::DeviceSize alignment = 16);

    // Makes `size` bytes written at `allocation` visible to the device; a no-op on host-coherent memory.
    void flush(const Allocation& allocation, vk::DeviceSize size) const;

    // Recycles the blocks the GPU is done with and frees the ones left unused for a while. Call once per frame.
    void collect();

    // Frees every block. Only valid once the device is idle.
    void destroy();

private:
    struct Block {
        vk::Buffer buffer;
        VmaAllocation allocation = nullptr;
        std::byte* mapping = nullptr;
        vk::DeviceSize capacity = 0;
        vk::DeviceSize head = 0;
        uint64_t lastSerial = 0;  // last command buffer that copies from this block
        uint32_t idleFrames = 0;  // collect() calls spent in the free list
    };

    static constexpr vk::DeviceSize kBlockSize = 4 * 1024 * 1024;
    // Up to kMaxFreeBlocks standard blocks are kept for reuse, each for at most kIdleFramesBeforeRelease unused frames.
    // Larger blocks (one-off uploads such as big textures) are freed as soon as the GPU is done with them.
    static constexpr size_t kMaxFreeBlocks = 4;
    static constexpr uint32_t kIdleFramesBeforeRelease = 300;

    Block acquireBlock(vk::DeviceSize minCapacity);
    void destroyBlock(Block& block);

    VmaAllocator m_allocator;
    CommandsPool& m_commands;

    Block m_current;              // receives new allocations; empty until the first upload
    std::vector<Block> m_pending; // retired blocks the GPU may still copy from
    std::vector<Block> m_free;    // blocks ready for reuse
};

}
