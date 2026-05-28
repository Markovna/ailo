#pragma once

#include "RenderAPI.h"

namespace ailo {
class VertexBuffer;

class Shader;

class BufferObject {
 public:
  BufferObject(RenderAPI*, BufferBinding, size_t byteSize);
  void updateBuffer(RenderAPI*, const void* data, uint64_t byteSize, uint64_t byteOffset = 0);
  ~BufferObject();
  BufferHandle getHandle() const { return m_handle; }

 private:
  RenderAPI* m_renderAPI;
  BufferHandle m_handle;
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
 VertexBuffer(RenderAPI*, const VertexInputDescription& description, size_t byteSize);
 void updateBuffer(RenderAPI*, const void* data, uint64_t byteSize, uint64_t byteOffset = 0);
 ~VertexBuffer();

 BufferHandle getBuffer() const { return m_bufferHandle; }
 VertexBufferLayoutHandle getLayout() const { return m_layoutHandle; }

private:
 RenderAPI* m_renderAPI;
 VertexBufferLayoutHandle m_layoutHandle;
 BufferHandle m_bufferHandle;
};

}
