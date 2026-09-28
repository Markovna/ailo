#pragma once

#include <unordered_map>

#include <vulkan/vulkan.hpp>
#include "render/vulkan/Resources.h"

#include "utils/Utils.h"

namespace ailo {

// Samplers are few and small, so they are never evicted: every sampler lives until clear(),
// which must only be called once no submitted command can reference them.
class SamplerCache {
    struct SamplerParamsHash {
        size_t operator()(const SamplerParams& params) const {
            size_t seed = 0;
            utils::hash_combine(seed, params.filterMag);
            utils::hash_combine(seed, params.filterMin);
            utils::hash_combine(seed, params.mipmapMode);
            utils::hash_combine(seed, params.wrapS);
            utils::hash_combine(seed, params.wrapT);
            utils::hash_combine(seed, params.wrapR);
            utils::hash_combine(seed, params.compareMode);
            utils::hash_combine(seed, params.compareFunc);
            return seed;
        }
    };

public:
    SamplerCache(vk::Device device, vk::PhysicalDevice physicalDevice);
    ~SamplerCache();

    SamplerCache(const SamplerCache&) = delete;
    SamplerCache& operator=(const SamplerCache&) = delete;

    vk::Sampler getOrCreate(const SamplerParams& params);

    void clear();

private:
    vk::Device m_device;
    float m_maxAnisotropy;
    std::unordered_map<SamplerParams, vk::Sampler, SamplerParamsHash> m_cache;
};

}
