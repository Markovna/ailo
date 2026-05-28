#include "RenderPrimitive.h"

namespace ailo {

BufferObject::BufferObject(RenderAPI* renderApi, BufferBinding binding, size_t byteSize) : m_renderAPI(renderApi) {
  m_handle = m_renderAPI->createBuffer(binding, byteSize);
}

void BufferObject::updateBuffer(RenderAPI* renderApi, const void* data, uint64_t byteSize, uint64_t byteOffset) {
  renderApi->updateBuffer(m_handle, data, byteSize, byteOffset);
}

BufferObject::~BufferObject() {
  m_renderAPI->destroyBuffer(m_handle);
}

VertexBuffer::VertexBuffer(RenderAPI* renderApi, const VertexInputDescription& description, size_t byteSize) : m_renderAPI(renderApi) {
  m_layoutHandle = m_renderAPI->createVertexBufferLayout(description);
  m_bufferHandle = m_renderAPI->createBuffer(BufferBinding::VERTEX, byteSize);
}

void VertexBuffer::updateBuffer(RenderAPI* renderApi, const void* data, uint64_t byteSize, uint64_t byteOffset) {
  renderApi->updateBuffer(m_bufferHandle, data, byteSize, byteOffset);
}

VertexBuffer::~VertexBuffer() {
  m_renderAPI->destroyVertexBufferLayout(m_layoutHandle);
  m_renderAPI->destroyBuffer(m_bufferHandle);
}

}
