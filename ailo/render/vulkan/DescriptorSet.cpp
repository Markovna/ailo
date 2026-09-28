#include "DescriptorSet.h"

#include <cassert>
#include <utility>

#include "Texture.h"

namespace ailo::gpu {

DescriptorSet::DescriptorSet(vk::Device device, vk::DescriptorPool pool, Shared<DescriptorSetLayout> layout)
    : m_device(device), m_pool(pool), m_layout(std::move(layout)) {
    assert(m_layout);
    m_descriptorSet = allocate();
}

DescriptorSet::~DescriptorSet() {
    if (m_descriptorSet) {
        (void) m_device.freeDescriptorSets(m_pool, 1, &m_descriptorSet);
    }
}

void DescriptorSet::updateBuffer(uint32_t binding, Shared<Buffer> buffer, uint64_t offset, uint64_t size) {
    vk::DescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = buffer->buffer;
    bufferInfo.offset = offset;
    bufferInfo.range = size == kWholeSize ? buffer->size : size;

    const bool isDynamic = m_layout->dynamicBindings[binding];

    vk::WriteDescriptorSet descriptorWrite{};
    descriptorWrite.dstBinding = binding;
    descriptorWrite.descriptorType = isDynamic ? vk::DescriptorType::eUniformBufferDynamic : vk::DescriptorType::eUniformBuffer;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pBufferInfo = &bufferInfo;

    write(descriptorWrite, true, std::move(buffer));
}

void DescriptorSet::updateTexture(uint32_t binding, Shared<Texture> texture, vk::Sampler sampler) {
    vk::DescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    imageInfo.imageView = texture->imageView;
    imageInfo.sampler = sampler;

    vk::WriteDescriptorSet descriptorWrite{};
    descriptorWrite.dstBinding = binding;
    descriptorWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageInfo;

    write(descriptorWrite, false, std::move(texture));
}

vk::DescriptorSet DescriptorSet::reallocate() {
    vk::DescriptorSet newDescriptorSet = allocate();

    std::vector<vk::CopyDescriptorSet> copyDescriptors;
    for (uint32_t i = 0; i < m_writtenBindings.size(); i++) {
        if (!m_writtenBindings[i]) {
            continue;
        }
        vk::CopyDescriptorSet copyDescriptorSet {};
        copyDescriptorSet.srcSet = m_descriptorSet;
        copyDescriptorSet.srcBinding = i;
        copyDescriptorSet.srcArrayElement = 0;
        copyDescriptorSet.dstSet = newDescriptorSet;
        copyDescriptorSet.dstBinding = i;
        copyDescriptorSet.dstArrayElement = 0;
        copyDescriptorSet.descriptorCount = 1;

        copyDescriptors.push_back(copyDescriptorSet);
    }

    m_device.updateDescriptorSets(0, nullptr, static_cast<uint32_t>(copyDescriptors.size()), copyDescriptors.data());

    m_lastUsedSerial = 0;
    return std::exchange(m_descriptorSet, newDescriptorSet);
}

vk::DescriptorSet DescriptorSet::allocate() const {
    vk::DescriptorSetAllocateInfo allocInfo{};
    allocInfo.descriptorPool = m_pool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_layout->layout;

    return m_device.allocateDescriptorSets(allocInfo)[0];
}

void DescriptorSet::write(const vk::WriteDescriptorSet& write, bool isBuffer, Shared<Resource> resource) {
    const uint32_t binding = write.dstBinding;

    vk::WriteDescriptorSet descriptorWrite = write;
    descriptorWrite.dstSet = m_descriptorSet;
    m_device.updateDescriptorSets(1, &descriptorWrite, 0, nullptr);

    m_bufferBindings[binding] = isBuffer;
    m_writtenBindings[binding] = true;

    if (m_boundResources.size() <= binding) {
        m_boundResources.resize(binding + 1);
    }
    // The previous resource may still be read by submitted commands, but those hold their own references.
    m_boundResources[binding] = std::move(resource);
}

}
