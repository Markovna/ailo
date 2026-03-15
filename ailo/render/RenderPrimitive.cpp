#include "RenderPrimitive.h"
#include "Engine.h"

namespace ailo {

BufferObject::BufferObject(Engine& engine, BufferBinding binding, size_t byteSize) : m_renderAPI(engine.getRenderAPI()) {
  m_handle = m_renderAPI->createBuffer(binding, byteSize);
}

void BufferObject::updateBuffer(Engine& engine, const void* data, uint64_t byteSize, uint64_t byteOffset) {
  engine.getRenderAPI()->updateBuffer(m_handle, data, byteSize, byteOffset);
}

BufferObject::~BufferObject() {
  m_renderAPI->destroyBuffer(m_handle);
}

VertexBuffer::VertexBuffer(Engine& engine, const VertexInputDescription& description, size_t byteSize) : m_renderAPI(engine.getRenderAPI()) {
  m_layoutHandle = m_renderAPI->createVertexBufferLayout(description);
  m_bufferHandle = m_renderAPI->createBuffer(BufferBinding::VERTEX, byteSize);
}

void VertexBuffer::updateBuffer(Engine& engine, const void* data, uint64_t byteSize, uint64_t byteOffset) {
  engine.getRenderAPI()->updateBuffer(m_bufferHandle, data, byteSize, byteOffset);
}

VertexBuffer::~VertexBuffer() {
  m_renderAPI->destroyVertexBufferLayout(m_layoutHandle);
  m_renderAPI->destroyBuffer(m_bufferHandle);
}

}
