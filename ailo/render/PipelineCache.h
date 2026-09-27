#pragma once

#include <vulkan/vulkan.hpp>
#include <array>

#include "render/Resource.h"
#include "render/Program.h"
#include "utils/Utils.h"
#include "common/LRUCache.h"

namespace ailo {

class Pipeline : public Resource {
public:
    Pipeline(vk::Device device, const Shared<gpu::Program>& program, vk::RenderPass renderPass, const gpu::VertexInputLayout& vertexInput, const gpu::FrameBufferFormat& format);
    ~Pipeline();

    vk::Pipeline operator*() const noexcept { return m_pipeline; }
    operator vk::Pipeline() const noexcept { return m_pipeline; }

private:
    // Holding a reference here prevents the program from being destroyed until the pipeline
    // has been destroyed. This is necessary to ensure that the PipelineCacheKey containing
    // the program handle always corresponds to the correct program.
    Shared<gpu::Program> m_programPtr;
    vk::Pipeline m_pipeline;
    vk::Device m_device;
};

class PipelineCache {
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

    struct PipelineState {
        Shared<gpu::Program> program {};
        gpu::VertexInputLayout vertexLayout {};
        vk::RenderPass renderPass {};
        gpu::FrameBufferFormat frameBufferFormat {};
    };

public:
    static constexpr size_t kDefaultCacheSize = 256;

    explicit PipelineCache(vk::Device device, ResourceContainer<Pipeline>& pipelines);

    void bindProgram(const Shared<gpu::Program>& program);
    void bindVertexLayout(const gpu::VertexInputLayout& vertexLayout) { m_pipelineState.vertexLayout = vertexLayout; }
    void bindRenderPass(vk::RenderPass renderPass, const gpu::FrameBufferFormat& format) {
        m_pipelineState.renderPass = renderPass;
        m_pipelineState.frameBufferFormat = format;
    }

    vk::PipelineLayout pipelineLayout() const { return m_pipelineState.program->pipelineLayout(); }

    Shared<Pipeline> getOrCreate();

    void clear() {
        m_cache.clear();
        m_pipelineState = {};
    }


private:
    ResourceContainer<Pipeline>* m_pipelines;
    LRUCache<PipelineCacheQuery, Shared<Pipeline>, PipelineCacheQueryHash> m_cache;
    vk::Device m_device;
    PipelineState m_pipelineState;
};

}
