#pragma once

#include <Engine.h>

#include "Material.h"
#include "RenderAPI.h"

namespace ailo {

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

class RenderPrimitive {
 public:
  explicit RenderPrimitive(
        BufferObject* vertexBuffer = nullptr,
        BufferObject* indexBuffer = nullptr,
        Material* material = nullptr,
        size_t indexOffset = 0,
        size_t indexCount = 0);

  const BufferObject* getVertexBuffer() const { return m_vertexBuffer; }
  const BufferObject* getIndexBuffer() const { return m_indexBuffer; }

  void setVertexBuffer(BufferObject* buffer) { m_vertexBuffer = buffer; }
  void setIndexBuffer(BufferObject* buffer, size_t offset, size_t count);

  auto getIndexCount() const { return m_indexCount; }
  auto getIndexOffset() const { return m_indexOffset; }

  Material* getMaterial();
  const Material* getMaterial() const;
  void setMaterial(Material* material);

 private:
  friend class Renderer;

  BufferObject* m_vertexBuffer;
  BufferObject* m_indexBuffer;
  Material* m_material;
  size_t m_indexOffset;
  size_t m_indexCount;
};

}
