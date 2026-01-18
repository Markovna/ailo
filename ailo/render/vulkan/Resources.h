#pragma once
#include <memory>
#include <bitset>

#include "vulkan/vulkan.hpp"
#include "vma/vk_mem_alloc.h"
#include "render/ResourcePtr.h"
#include "render/Constants.h"

namespace ailo {
class FenceStatus;

namespace gpu {
struct VertexBufferLayout;

class Program;
struct Buffer;
struct DescriptorSet;
class Texture;
struct DescriptorSetLayout;

}

using ProgramHandle = Handle<gpu::Program>;
using BufferHandle = Handle<gpu::Buffer>;
using VertexBufferLayoutHandle = Handle<gpu::VertexBufferLayout>;
using DescriptorSetHandle = Handle<gpu::DescriptorSet>;
using TextureHandle = Handle<gpu::Texture>;
using DescriptorSetLayoutHandle = Handle<gpu::DescriptorSetLayout>;

enum class BufferBinding : uint8_t {
  UNKNOWN,
  VERTEX,
  INDEX,
  UNIFORM,
};

enum class CullingMode : uint8_t {
  NONE,
  FRONT,
  BACK,
  FRONT_AND_BACK
};

enum class BlendOperation : uint8_t {
  ADD,
  SUBTRACT,
  REVERSE_SUBTRACT,
  MIN,
  MAX
};

enum class BlendFunction : uint8_t {
  ZERO,
  ONE,
  SRC_COLOR,
  ONE_MINUS_SRC_COLOR,
  DST_COLOR,
  ONE_MINUS_DST_COLOR,
  SRC_ALPHA,
  ONE_MINUS_SRC_ALPHA,
  DST_ALPHA,
  ONE_MINUS_DST_ALPHA,
  SRC_ALPHA_SATURATE
};

enum class CompareOp : uint8_t {
  NEVER = 0,
  LESS,
  EQUAL,
  LESS_OR_EQUAL,
  GREATER,
  NOT_EQUAL,
  GREATER_OR_EQUAL,
  ALWAYS
};

class Acquirable {
public:
    void setFence(const std::shared_ptr<FenceStatus>& fence) { m_fenceStatus = fence; }
    bool isAcquired() const;

private:
    std::shared_ptr<FenceStatus> m_fenceStatus;
};

template<typename T>
class PerColorAttachment : public std::array<T, kMaxColorAttachments> {

};

namespace gpu {

struct Buffer {
    vk::Buffer buffer;
    uint64_t size;
    VmaAllocation vmaAllocation;
    VmaAllocationInfo allocationInfo;
    BufferBinding binding;
};

struct VertexBufferLayout {
    static constexpr uint32_t kMaxAttributes = 8;

    std::array<vk::VertexInputBindingDescription, kMaxAttributes> bindings;
    std::array<vk::VertexInputAttributeDescription, kMaxAttributes> attributes;
    size_t attributesCount;
    size_t bindingsCount;
};

struct StageBuffer : public Acquirable {
    vk::Buffer buffer;
    uint64_t size;
    VmaAllocation vmaAllocation;
    void* mapping;
};

struct DescriptorSetLayout {
    using bitmask_t = std::bitset<64>;

    vk::DescriptorSetLayout layout;
    bitmask_t dynamicBindings;
};

struct DescriptorSet {
    vk::DescriptorSet descriptorSet;
    DescriptorSetLayout::bitmask_t boundBindings;
    DescriptorSetLayout::bitmask_t dynamicBindings;
    DescriptorSetLayoutHandle layoutHandle;
    std::shared_ptr<FenceStatus> boundFence;

    bool isBound() const;
};

}

struct RenderPassAttachmentOperations {
    vk::AttachmentLoadOp load;
    vk::AttachmentStoreOp store;
};

struct RenderPassDescription {
    PerColorAttachment<RenderPassAttachmentOperations> color;
    RenderPassAttachmentOperations depth;
};

struct VertexInputDescription {
    std::vector<vk::VertexInputBindingDescription> bindings;
    std::vector<vk::VertexInputAttributeDescription> attributes;
};

struct RasterDescription {
    CullingMode cullingMode = CullingMode::FRONT;
    bool inverseFrontFace = false;
    bool blendEnable = false;
    bool depthWriteEnable = true;
    BlendOperation rgbBlendOp;
    BlendOperation alphaBlendOp;
    BlendFunction srcRgbBlendFunc;
    BlendFunction srcAlphaBlendFunc;
    BlendFunction dstRgbBlendFunc;
    BlendFunction dstAlphaBlendFunc;
    CompareOp depthCompareOp = CompareOp::LESS;
};

struct DescriptorSetLayoutBinding {
    uint32_t binding;
    vk::DescriptorType descriptorType;
    vk::ShaderStageFlags stageFlags;
};

struct ShaderDescription {
    using ShaderCode = std::vector<char>;
    using SetLayout = std::vector<DescriptorSetLayoutBinding>;

    ShaderCode vertexShader;
    ShaderCode fragmentShader;
    RasterDescription raster;
    std::vector<SetLayout> layout;
};

struct PipelineDescription {
  ShaderDescription shader;
  VertexInputDescription vertexInput;
};

struct PipelineState {
    Handle<gpu::Program> program;
    Handle<gpu::VertexBufferLayout> vertexBufferLayout;
};


}
