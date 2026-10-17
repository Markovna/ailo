#include "RenderPrimitive.h"

#include "RenderAPI.h"

namespace ailo {

BufferObject::BufferObject(RenderAPI* renderApi, BufferBinding binding, size_t byteSize)
  : m_buffer(renderApi->createBuffer(binding, byteSize)) {
}

void BufferObject::updateBuffer(RenderAPI* renderApi, const void* data, uint64_t byteSize, uint64_t byteOffset) {
  renderApi->updateBuffer(m_buffer, data, byteSize, byteOffset);
}

IndexBuffer::IndexBuffer(RenderAPI* renderApi, IndexType indexType, uint32_t indexCount)
  : m_buffer(renderApi->createBuffer(BufferBinding::INDEX, uint64_t(indexCount) * indexSize(indexType))),
    m_indexType(indexType) {
}

void IndexBuffer::updateBuffer(RenderAPI* renderApi, const void* data, uint32_t indexCount, uint32_t firstIndex) {
  const uint32_t size = indexSize(m_indexType);
  renderApi->updateBuffer(m_buffer, data, uint64_t(indexCount) * size, uint64_t(firstIndex) * size);
}

uint32_t indexSize(IndexType indexType) {
  return indexType == IndexType::UINT32 ? sizeof(uint32_t) : sizeof(uint16_t);
}

VertexBuffer::VertexBuffer(RenderAPI* renderApi, const VertexInputDescription& description, size_t byteSize)
  : m_layout(renderApi->createVertexBufferLayout(description)),
    m_buffer(renderApi->createBuffer(BufferBinding::VERTEX, byteSize)) {
}

void VertexBuffer::updateBuffer(RenderAPI* renderApi, const void* data, uint64_t byteSize, uint64_t byteOffset) {
  renderApi->updateBuffer(m_buffer, data, byteSize, byteOffset);
}

}
