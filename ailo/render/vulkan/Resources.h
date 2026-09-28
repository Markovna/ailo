#pragma once
#include <memory>
#include <bitset>
#include <vector>

#include "vulkan/vulkan.hpp"
#include "vma/vk_mem_alloc.h"
#include "render/Resource.h"
#include "render/Constants.h"

namespace ailo {

namespace gpu {
struct RenderTarget;
struct VertexBufferLayout;

class Program;
struct Buffer;
class DescriptorSet;
class Texture;
struct DescriptorSetLayout;

}

using ProgramHandle = Handle<gpu::Program>;
using BufferHandle = Handle<gpu::Buffer>;
using VertexBufferLayoutHandle = Handle<gpu::VertexBufferLayout>;
using DescriptorSetHandle = Handle<gpu::DescriptorSet>;
using TextureHandle = Handle<gpu::Texture>;
using DescriptorSetLayoutHandle = Handle<gpu::DescriptorSetLayout>;
using RenderTargetHandle = Handle<gpu::RenderTarget>;

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

enum class TextureType : uint8_t {
    TEXTURE_2D,
    TEXTURE_CUBEMAP
};

enum class TextureUsage : uint16_t {
    None = 0,
    Sampled = 1 << 0,
    Storage = 1 << 1,
    ColorAttachment = 1 << 2,
    DepthStencilAttachment = 1 << 3,
};

inline TextureUsage operator&(TextureUsage lhs, TextureUsage rhs) {
    return static_cast<TextureUsage>(static_cast<uint16_t>(lhs) & static_cast<uint16_t>(rhs));
}
inline TextureUsage operator|(TextureUsage lhs, TextureUsage rhs) {
    return static_cast<TextureUsage>(static_cast<uint16_t>(lhs) | static_cast<uint16_t>(rhs));
}

class ColorAttachmentMask : public std::bitset<kMaxColorAttachments> {};

template<typename T>
class PerColorAttachment : public std::array<T, kMaxColorAttachments> {

};

namespace gpu {

struct Buffer : Resource {
    ~Buffer() {
        if (buffer) {
            vmaDestroyBuffer(allocator, buffer, vmaAllocation);
        }
    }

    VmaAllocator allocator {};
    vk::Buffer buffer;
    uint64_t size;
    VmaAllocation vmaAllocation;
    VmaAllocationInfo allocationInfo;
    BufferBinding binding;
    // Serial of the last command buffer that reads the buffer (vertex/index input or a bound descriptor set);
    // 0 while nothing has read it. Lets an upload skip the barrier against reads that have already completed.
    uint64_t lastReadSerial = 0;
};

struct VertexInputLayout {
    static constexpr uint32_t kMaxAttributes = 8;

    std::array<vk::VertexInputBindingDescription, kMaxAttributes> bindings;
    std::array<vk::VertexInputAttributeDescription, kMaxAttributes> attributes;
    size_t attributesCount;
    size_t bindingsCount;
};

struct VertexBufferLayout : Resource, VertexInputLayout {};

struct DescriptorSetLayout : Resource {
    using bitmask_t = std::bitset<64>;

    ~DescriptorSetLayout() {
        if (layout) {
            device.destroyDescriptorSetLayout(layout);
        }
    }

    vk::Device device;
    vk::DescriptorSetLayout layout;
    bitmask_t dynamicBindings;
};

struct RenderTarget : Resource {
    PerColorAttachment<Shared<Texture>> colors {};
    PerColorAttachment<Shared<Texture>> resolve {};
    Shared<Texture> depth {};
    uint32_t width {};
    uint32_t height {};
    vk::SampleCountFlagBits samples {};
};

struct FrameBufferFormat {
    PerColorAttachment<vk::Format> color {};
    vk::Format depth = vk::Format::eUndefined;
    ColorAttachmentMask hasResolve {};
    vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1;
};

struct FrameBufferImageView {
    PerColorAttachment<vk::ImageView> color {};
    PerColorAttachment<vk::ImageView> resolve {};
    vk::ImageView depth {};
};

}

struct RenderPassAttachmentOperations {
    vk::AttachmentLoadOp load = vk::AttachmentLoadOp::eDontCare;
    vk::AttachmentStoreOp store = vk::AttachmentStoreOp::eDontCare;
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

struct RenderPassState {
    Shared<gpu::RenderTarget> renderTarget {};
};

}
