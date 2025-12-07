#pragma once

#include <Engine.h>
#include "RenderAPI.h"

namespace ailo {

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
        size_t indexOffset = 0,
        size_t indexCount = 0)
      : m_vertexBuffer(vertexBuffer)
      , m_indexBuffer(indexBuffer)
      , m_indexOffset(indexOffset)
      , m_indexCount(indexCount)
      , m_transform(1)
      { }

  const BufferObject* getVertexBuffer() const { return m_vertexBuffer; }
  const BufferObject* getIndexBuffer() const { return m_indexBuffer; }

  void setVertexBuffer(BufferObject* buffer) { m_vertexBuffer = buffer; }
  void setIndexBuffer(BufferObject* buffer, size_t offset, size_t count);

  const glm::mat4& getTransform() const { return m_transform; }
  void setTransform(const glm::mat4& tr) { m_transform = tr; }

  auto getIndexCount() const { return m_indexCount; }
  auto getIndexOffset() const { return m_indexOffset; }

  //TODO: remove from primitive, store in renderer
  DescriptorSetHandle& getDescriptorSet() const { return m_dsh; }

  PipelineHandle getPipeline() { return m_pipeline; }
  void setPipeline(PipelineHandle handle) { m_pipeline = handle; }

 private:
  friend class Renderer;

  BufferObject* m_vertexBuffer;
  BufferObject* m_indexBuffer;
  glm::mat4 m_transform;
  size_t m_indexOffset;
  size_t m_indexCount;

  mutable DescriptorSetHandle m_dsh;
  PipelineHandle m_pipeline;
};

}
