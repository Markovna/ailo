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

void RenderPrimitive::setIndexBuffer(BufferObject* buffer, size_t offset, size_t count) {
  m_indexBuffer = buffer;
  m_indexCount = count;
  m_indexOffset = offset;
}

}