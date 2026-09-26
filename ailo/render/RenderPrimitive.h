#pragma once

#include "RenderAPI.h"

namespace ailo {
class VertexBuffer;

class Shader;

class BufferObject {
 public:
  BufferObject() = default;
  BufferObject(RenderAPI*, BufferBinding, size_t byteSize);
  void updateBuffer(RenderAPI*, const void* data, uint64_t byteSize, uint64_t byteOffset = 0);
  BufferHandle getHandle() const { return m_buffer; }

 private:
  Unique<gpu::Buffer> m_buffer;
};

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
