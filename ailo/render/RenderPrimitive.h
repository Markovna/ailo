#pragma once

#include <cstdint>

#include "render/vulkan/Resources.h"

namespace ailo {

class RenderAPI;
class VertexBuffer;
class Shader;

class BufferObject {
 public:
  BufferObject() = default;
  BufferObject(RenderAPI*, BufferBinding, size_t byteSize);
  void updateBuffer(RenderAPI*, const void* data, uint64_t byteSize, uint64_t byteOffset = 0);
  BufferHandle getHandle() const { return m_buffer; }
  Shared<gpu::Buffer>& shared() { return m_buffer; }

 private:
  Shared<gpu::Buffer> m_buffer;
};

class IndexBuffer {
 public:
  IndexBuffer() = default;
  IndexBuffer(RenderAPI*, IndexType, uint32_t indexCount);
  void updateBuffer(RenderAPI*, const void* data, uint32_t indexCount, uint32_t firstIndex = 0);
  BufferHandle getHandle() const { return m_buffer; }
  IndexType getIndexType() const { return m_indexType; }

 private:
  Unique<gpu::Buffer> m_buffer;
  IndexType m_indexType = IndexType::UINT16;
};

uint32_t indexSize(IndexType);

enum class VertexLocation {
  Position = 0,
  Color = 1,
  TexCoord = 2,
  Normal = 3,
  Tangent = 4,
  BoneIndices = 5,
  BoneWeights = 6,

  Count
};

class VertexBuffer {
public:
 VertexBuffer() = default;
 VertexBuffer(RenderAPI*, const VertexInputDescription& description, size_t byteSize);
 void updateBuffer(RenderAPI*, const void* data, uint64_t byteSize, uint64_t byteOffset = 0);

 BufferHandle getBuffer() const { return m_buffer; }
 VertexBufferLayoutHandle getLayout() const { return m_layout; }

private:
 Unique<gpu::VertexBufferLayout> m_layout;
 Unique<gpu::Buffer> m_buffer;
};

}
