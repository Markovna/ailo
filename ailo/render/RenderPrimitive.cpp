#include "RenderPrimitive.h"

namespace ailo {

BufferObject::BufferObject(RenderAPI* renderApi, BufferBinding binding, size_t byteSize)
  : m_buffer(renderApi->createBuffer(binding, byteSize)) {
}

void BufferObject::updateBuffer(RenderAPI* renderApi, const void* data, uint64_t byteSize, uint64_t byteOffset) {
  renderApi->updateBuffer(m_buffer, data, byteSize, byteOffset);
}

VertexBuffer::VertexBuffer(RenderAPI* renderApi, const VertexInputDescription& description, size_t byteSize)
  : m_layout(renderApi->createVertexBufferLayout(description)),
    m_buffer(renderApi->createBuffer(BufferBinding::VERTEX, byteSize)) {
}

void VertexBuffer::updateBuffer(RenderAPI* renderApi, const void* data, uint64_t byteSize, uint64_t byteOffset) {
  renderApi->updateBuffer(m_buffer, data, byteSize, byteOffset);
}

}
