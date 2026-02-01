#pragma once

#include <Engine.h>

#include "Material.h"
#include "RenderAPI.h"

namespace ailo {
class VertexBuffer;

class Shader;

class BufferObject {
 public:
  BufferObject(Engine&, BufferBinding, size_t byteSize);
  void updateBuffer(Engine&, const void* data, uint64_t byteSize, uint64_t byteOffset = 0);
  void destroy(Engine&);
  BufferHandle getHandle() const { return m_handle; }

 private:
  BufferHandle m_handle;
};

class VertexBuffer {
public:
 VertexBuffer(Engine&, const VertexInputDescription& description, size_t byteSize);
 void updateBuffer(Engine&, const void* data, uint64_t byteSize, uint64_t byteOffset = 0);
 void destroy(Engine&);

 BufferHandle getBuffer() const { return m_bufferHandle; }
 VertexBufferLayoutHandle getLayout() const { return m_layoutHandle; }

private:
 VertexBufferLayoutHandle m_layoutHandle;
 BufferHandle m_bufferHandle;
};

class RenderPrimitive {
 public:
  explicit RenderPrimitive(
        std::shared_ptr<Material> material = nullptr,
        size_t indexOffset = 0,
        size_t indexCount = 0);

  auto getIndexCount() const { return m_indexCount; }
  auto getIndexOffset() const { return m_indexOffset; }

  const Material* getMaterial() const;
  Material* getMaterial();
  void setMaterial(std::shared_ptr<Material> material);

 private:
  std::shared_ptr<Material> m_material;
  size_t m_indexOffset;
  size_t m_indexCount;
};

}
