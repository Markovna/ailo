#include "Resources.h"
#include "render/CommandBuffer.h"

bool ailo::Acquirable::isAcquired() const { return m_fenceStatus && !m_fenceStatus->isSignaled(); }

bool ailo::gpu::DescriptorSet::isBound() const { return boundFence && !boundFence->isSignaled(); }
