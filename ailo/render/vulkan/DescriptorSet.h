#pragma once

#include <cstdint>
#include <limits>
#include <vector>

#include <vulkan/vulkan.hpp>

#include "Resources.h"
#include "render/Resource.h"

namespace ailo::gpu {

class DescriptorSet : public Resource {
public:
    using bitmask_t = DescriptorSetLayout::bitmask_t;

    static constexpr uint64_t kWholeSize = std::numeric_limits<uint64_t>::max();

    DescriptorSet(vk::Device device, vk::DescriptorPool pool, Shared<DescriptorSetLayout> layout);
    ~DescriptorSet();

    void updateBuffer(uint32_t binding, Shared<Buffer> buffer, uint64_t offset = 0, uint64_t size = kWholeSize);
    void updateTexture(uint32_t binding, Shared<Texture> texture);

    [[nodiscard]] vk::DescriptorSet reallocate();

    uint64_t getLastUsedSerial() const { return m_lastUsedSerial; }
    void setLastUsedSerial(uint64_t serial) { m_lastUsedSerial = serial; }

    vk::DescriptorSet getHandle() const { return m_descriptorSet; }
    DescriptorSetLayoutHandle getLayoutHandle() const { return m_layout.getHandle(); }

    bool isBufferBinding(uint32_t binding) const { return m_bufferBindings[binding]; }

    const std::vector<Shared<Resource>>& getBoundResources() const { return m_boundResources; }

private:
    vk::DescriptorSet allocate() const;
    void write(const vk::WriteDescriptorSet& write, bool isBuffer, Shared<Resource> resource);

    vk::Device m_device;
    vk::DescriptorPool m_pool;
    Shared<DescriptorSetLayout> m_layout;
    vk::DescriptorSet m_descriptorSet;
    bitmask_t m_bufferBindings;
    bitmask_t m_writtenBindings;
    uint64_t m_lastUsedSerial = 0;
    std::vector<Shared<Resource>> m_boundResources;
};

}
