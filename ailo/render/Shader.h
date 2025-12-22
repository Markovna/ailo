#pragma once

#include "RenderAPI.h"
#include "utils/Utils.h"

namespace ailo {

class Engine;

struct PipelineCacheKey {
  std::array<vk::VertexInputBindingDescription, 16> vertexBindings;
  std::array<vk::VertexInputAttributeDescription, 16> vertexAttributes;
};

struct PipelineCacheKeyHasher {
    std::size_t operator()(const PipelineCacheKey& key) const {
        size_t seed = 0;
        for (auto& binding : key.vertexBindings) {
            utils::hash_combine(seed, binding.binding);
            utils::hash_combine(seed, binding.inputRate);
            utils::hash_combine(seed, binding.stride);
        }

        for (auto& attribute : key.vertexAttributes) {
            utils::hash_combine(seed, attribute.binding);
            utils::hash_combine(seed, attribute.location);
            utils::hash_combine(seed, attribute.format);
            utils::hash_combine(seed, attribute.offset);
        }
        return seed;
    }
};

struct PipelineCacheKeyEqual {
    bool operator()(const PipelineCacheKey& k1, const PipelineCacheKey& k2) const {
        return memcmp(&k1, &k2, sizeof(PipelineCacheKey)) == 0;
    }
};

class Shader {
 public:
    Shader(Engine&, const ShaderDescription&);
    void bindPipeline(Engine&, const VertexInputDescription&) const;

    DescriptorSetLayoutHandle getDescriptorSetLayout(uint32_t setIndex) const;

    void destroy(Engine&);

    static ShaderDescription& getDefaultShaderDescription();

 private:
    std::vector<DescriptorSetLayoutHandle> m_descriptorSetLayouts;
    mutable std::unordered_map<PipelineCacheKey, PipelineHandle, PipelineCacheKeyHasher, PipelineCacheKeyEqual> m_pipelines;
    ShaderDescription m_description;
};

}
