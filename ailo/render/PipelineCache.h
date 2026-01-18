#pragma once

#include <vulkan/vulkan.hpp>
#include <array>

#include "RenderPassCache.h"
#include "render/ResourcePtr.h"
#include "render/Constants.h"
#include "render/Program.h"
#include "utils/Utils.h"
#include "common/LRUCache.h"

namespace ailo {
struct RenderPassCacheQuery;

struct RenderPassCompatibilityKey {
    PerColorAttachment<vk::Format> colors;
    vk::Format depth;

    bool operator==(const RenderPassCompatibilityKey& other) const = default;
};

struct PipelineCacheQuery {
    static constexpr size_t kMaxAttributesCount = 8;

    uint64_t programHandle {};
    std::array<vk::VertexInputBindingDescription, kMaxAttributesCount> virtexBindings;
    std::array<vk::VertexInputAttributeDescription, kMaxAttributesCount> vertexAttributes;
    uint32_t vertexAttributesCount {};
    uint32_t vertexBindingsCount {};
    RenderPassCompatibilityKey renderPassKey {};

    bool operator==(const PipelineCacheQuery& other) const = default;
};

struct PipelineCacheQueryHash {
    std::size_t operator()(const PipelineCacheQuery& key) const {
        size_t seed = 0;
        utils::hash_combine(seed, key.programHandle);
        for (auto& binding : key.virtexBindings) {
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
        utils::hash_combine(seed, key.renderPassKey.depth);
        for (auto& color : key.renderPassKey.colors) {
            utils::hash_combine(seed, color);
        }
        return seed;
    }
};

class Pipeline : public enable_resource_ptr<Pipeline> {
public:
    Pipeline(vk::Device device, const resource_ptr<gpu::Program>& program, vk::RenderPass renderPass, const PipelineCacheQuery& key);
    ~Pipeline();

    vk::Pipeline getHandle() const { return m_pipeline; }

private:
    // Do not allow the program to be destroyed until the pipeline has been destroyed.
    // This is necessary to ensure that the PipelineCacheKey containing the program
    // handle always corresponds to the correct program. Having program ptr here will
    // prevent the program from being removed from the resource map and thus prevent
    // its handle from being reused.
    resource_ptr<gpu::Program> m_programPtr;
    vk::Pipeline m_pipeline;
    vk::Device m_device;
};

class PipelineCache {
public:
    static constexpr size_t kDefaultCacheSize = 256;

    explicit PipelineCache(vk::Device device, ResourceContainer<Pipeline>& pipelines);

    void bindProgram(const resource_ptr<gpu::Program>& program);
    void bindVertexLayout(const gpu::VertexBufferLayout& vertexLayout) { m_boundVertexLayout = vertexLayout; }
    void bindRenderPass(vk::RenderPass renderPass, const RenderPassCacheQuery& query) {
        m_boundRenderPass = renderPass;
        m_renderPassQuery = query;
    }

    vk::PipelineLayout pipelineLayout() const { return m_boundProgram->pipelineLayout(); }

    resource_ptr<Pipeline> getOrCreate();

    void clear() {
        m_cache.clear();
    }


private:
    ResourceContainer<Pipeline>* m_pipelines;
    LRUCache<PipelineCacheQuery, resource_ptr<Pipeline>, PipelineCacheQueryHash> m_cache;
    vk::Device m_device;

    resource_ptr<gpu::Program> m_boundProgram;
    gpu::VertexBufferLayout m_boundVertexLayout;
    vk::RenderPass m_boundRenderPass;
    RenderPassCacheQuery m_renderPassQuery;
};

}
