#include "SamplerCache.h"

#include "vulkan/VulkanUtils.h"

namespace ailo {

SamplerCache::SamplerCache(vk::Device device, vk::PhysicalDevice physicalDevice)
    : m_device(device), m_maxAnisotropy(physicalDevice.getProperties().limits.maxSamplerAnisotropy) {}

SamplerCache::~SamplerCache() {
    clear();
}

vk::Sampler SamplerCache::getOrCreate(const SamplerParams& params) {
    if (auto it = m_cache.find(params); it != m_cache.end()) {
        return it->second;
    }

    vk::SamplerCreateInfo samplerInfo{};
    samplerInfo.magFilter = vkutils::getFilter(params.filterMag);
    samplerInfo.minFilter = vkutils::getFilter(params.filterMin);
    samplerInfo.mipmapMode = vkutils::getMipmapMode(params.mipmapMode);
    samplerInfo.addressModeU = vkutils::getWrapMode(params.wrapS);
    samplerInfo.addressModeV = vkutils::getWrapMode(params.wrapT);
    samplerInfo.addressModeW = vkutils::getWrapMode(params.wrapR);
    samplerInfo.compareEnable = params.compareMode == SamplerCompareMode::COMPARE_TO_TEXTURE;
    // Depth comparison samplers (shadow maps) have no mips and are sampled explicitly, anisotropy only costs.
    samplerInfo.anisotropyEnable = !samplerInfo.compareEnable;
    samplerInfo.maxAnisotropy = m_maxAnisotropy;
    samplerInfo.compareOp = vkutils::getCompareOperation(params.compareFunc);
    samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = VK_LOD_CLAMP_NONE;

    vk::Sampler sampler = m_device.createSampler(samplerInfo);
    m_cache.emplace(params, sampler);
    return sampler;
}

void SamplerCache::clear() {
    for (auto& [params, sampler] : m_cache) {
        m_device.destroySampler(sampler);
    }
    m_cache.clear();
}

}
