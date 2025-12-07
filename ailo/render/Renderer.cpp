#include "Renderer.h"
#include <ecs/Scene.h>

namespace ailo {

void Renderer::render(Engine& engine, Scene& scene, const Camera& camera) {
  // prepare per view buffer
  perViewUniformBufferData.projection = camera.projection;
  perViewUniformBufferData.view = camera.view;

  RenderAPI* backend = engine.getRenderAPI();
  backend->beginFrame();

  // prepare descriptor sets and uniform buffers
  prepare(*backend, scene);

  backend->beginRenderPass();

  uint32_t bufferOffset = 0;
  auto primitivesView = scene.getRegistry().view<RenderPrimitive>();
  for(auto [entity, primitive] : primitivesView.each()) {
    auto indexBuffer = primitive.getIndexBuffer();
    auto vertexBuffer = primitive.getVertexBuffer();
    auto pipeline = primitive.getPipeline();

    backend->bindPipeline(pipeline);

    backend->bindDescriptorSet(m_viewDescriptorSet, static_cast<uint32_t>(DescriptorSetBindingPoints::PER_VIEW));
    backend->bindDescriptorSet(m_objectDescriptorSet, static_cast<uint32_t>(DescriptorSetBindingPoints::PER_RENDERABLE), { bufferOffset });

    backend->bindIndexBuffer(indexBuffer->getHandle());
    backend->bindVertexBuffer(vertexBuffer->getHandle());
    backend->drawIndexed(primitive.getIndexCount(), 1, primitive.getIndexOffset());

    bufferOffset += sizeof(PerObjectUniforms);
  }

  backend->endRenderPass();
  backend->endFrame();
}

void Renderer::prepare(RenderAPI& backend, Scene& scene) {
  if(!m_viewUniformBufferHandle) {
    m_viewUniformBufferHandle = backend.createBuffer(BufferBinding::UNIFORM, sizeof(perViewUniformBufferData));
  }

  if(!m_viewDescriptorSetLayout) {
    DescriptorSetLayoutBinding viewUboBinding{};
    viewUboBinding.binding = 0;
    viewUboBinding.descriptorType = vk::DescriptorType::eUniformBuffer;
    viewUboBinding.stageFlags = vk::ShaderStageFlagBits::eVertex;

    m_viewDescriptorSetLayout = backend.createDescriptorSetLayout({ viewUboBinding });
  }

  if(!m_viewDescriptorSet) {
    m_viewDescriptorSet = backend.createDescriptorSet(m_viewDescriptorSetLayout);
    backend.updateDescriptorSetBuffer(m_viewDescriptorSet, m_viewUniformBufferHandle, 0);
  }

  if(!m_objectDescriptorSetLayout) {
    DescriptorSetLayoutBinding objectUboBinding{};
    objectUboBinding.binding = 0;
    objectUboBinding.descriptorType = vk::DescriptorType::eUniformBufferDynamic;
    objectUboBinding.stageFlags = vk::ShaderStageFlagBits::eVertex;

    m_objectDescriptorSetLayout = backend.createDescriptorSetLayout({ objectUboBinding });
  }

  size_t primitivesCount = 0;
  auto primitivesView = scene.getRegistry().view<RenderPrimitive>();
  for(auto [entity, primitive] : primitivesView.each()) {
    primitivesCount++;
  }

  // prepare per object buffer
  if(primitivesCount > perObjectUniformBufferData.size()) {
    perObjectUniformBufferData.resize(primitivesCount);

    backend.destroyBuffer(m_objectsUniformBufferHandle);
    m_objectsUniformBufferHandle = backend.createBuffer(BufferBinding::UNIFORM, perObjectUniformBufferData.size() * sizeof(PerObjectUniforms));

    backend.destroyDescriptorSet(m_objectDescriptorSet);
    m_objectDescriptorSet = { };
  }

  if(!m_objectDescriptorSet) {
    m_objectDescriptorSet = backend.createDescriptorSet(m_objectDescriptorSetLayout);
    backend.updateDescriptorSetBuffer(m_objectDescriptorSet, m_objectsUniformBufferHandle, 0);
  }

  uint32_t index = 0;
  for(auto [entity, primitive] : primitivesView.each()) {
    perObjectUniformBufferData[index].model = primitive.getTransform();
    index++;
  }

  backend.updateBuffer(m_viewUniformBufferHandle, &perViewUniformBufferData, sizeof(perViewUniformBufferData));
  backend.updateBuffer(m_objectsUniformBufferHandle, perObjectUniformBufferData.data(), perObjectUniformBufferData.size() * sizeof(PerObjectUniforms));
}

void Renderer::terminate(Engine& engine) {
  RenderAPI& backend = *engine.getRenderAPI();

  backend.destroyBuffer(m_viewUniformBufferHandle);
  backend.destroyBuffer(m_objectsUniformBufferHandle);

  backend.destroyDescriptorSet(m_viewDescriptorSet);
  backend.destroyDescriptorSet(m_objectDescriptorSet);

  backend.destroyDescriptorSetLayout(m_viewDescriptorSetLayout);
  backend.destroyDescriptorSetLayout(m_objectDescriptorSetLayout);
}

}