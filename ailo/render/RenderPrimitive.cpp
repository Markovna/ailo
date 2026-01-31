#include "RenderPrimitive.h"

namespace ailo {

BufferObject::BufferObject(Engine& engine, BufferBinding binding, size_t byteSize) {
  m_handle = engine.getRenderAPI()->createBuffer(binding, byteSize);
}

void BufferObject::updateBuffer(Engine& engine, const void* data, uint64_t byteSize, uint64_t byteOffset) {
  engine.getRenderAPI()->updateBuffer(m_handle, data, byteSize, byteOffset);
}

void BufferObject::destroy(Engine& engine) {
  engine.getRenderAPI()->destroyBuffer(m_handle);
}

RenderPrimitive::RenderPrimitive(VertexBuffer* vertexBuffer, BufferObject* indexBuffer, Material* material, size_t indexOffset,
  size_t indexCount) : m_vertexBuffer(vertexBuffer)
                      , m_indexBuffer(indexBuffer)
                      , m_material(material)
                      , m_indexOffset(indexOffset)
                      , m_indexCount(indexCount) { }

void RenderPrimitive::setIndexBuffer(BufferObject* buffer, size_t offset, size_t count) {
  m_indexBuffer = buffer;
  m_indexCount = count;
  m_indexOffset = offset;
}

const Material* RenderPrimitive::getMaterial() const {
  return m_material;
}

void RenderPrimitive::setMaterial(Material* material) { m_material = material; }

VertexBuffer::VertexBuffer(Engine& engine, const VertexInputDescription& description, size_t byteSize) {
  m_layoutHandle = engine.getRenderAPI()->createVertexBufferLayout(description);
  m_bufferHandle = engine.getRenderAPI()->createBuffer(BufferBinding::VERTEX, byteSize);
}

void VertexBuffer::updateBuffer(Engine& engine, const void* data, uint64_t byteSize, uint64_t byteOffset) {
  engine.getRenderAPI()->updateBuffer(m_bufferHandle, data, byteSize, byteOffset);
}

void VertexBuffer::destroy(Engine& engine) {
  engine.getRenderAPI()->destroyVertexBufferLayout(m_layoutHandle);
  engine.getRenderAPI()->destroyBuffer(m_bufferHandle);
}
}
