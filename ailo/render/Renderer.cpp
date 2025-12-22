#include "Renderer.h"
#include <ecs/Scene.h>

#include "Mesh.h"
#include "Shader.h"
#include "ecs/Transform.h"

namespace ailo {

void Renderer::render(Engine& engine, Scene& scene, const Camera& camera) {
  // prepare per view buffer
  perViewUniformBufferData.projection = camera.projection;
  perViewUniformBufferData.view = camera.view;
  perViewUniformBufferData.viewInverse = inverse(camera.view);
  perViewUniformBufferData.lightColorIntensity = glm::vec4(1.0f, 1.0f, 1.0f, 0.8f);
  perViewUniformBufferData.lightDirection = normalize(glm::vec3(1.0f, 3.0f, -1.0f));

  RenderAPI* backend = engine.getRenderAPI();
  backend->beginFrame();

  // prepare descriptor sets and uniform buffers
  prepare(*backend, scene);

  backend->beginRenderPass();

  uint32_t bufferOffset = 0;
  auto meshView = scene.view<Mesh>();
  for(const auto& [entity, mesh] : meshView.each()) {
    auto indexBuffer = mesh.indexBuffer.get();
    auto vertexBuffer = mesh.vertexBuffer.get();

    for(const auto& primitive : mesh.primitives) {
      auto material = primitive.getMaterial();
      auto shader = material->getShader();

      shader->bindPipeline(engine, mesh.vertexInput);

      backend->bindDescriptorSet(m_viewDescriptorSet, std::to_underlying(DescriptorSetBindingPoints::PER_VIEW));
      backend->bindDescriptorSet(m_objectDescriptorSet, std::to_underlying(DescriptorSetBindingPoints::PER_RENDERABLE), { bufferOffset });
      material->bindDescriptorSet(*backend);

      backend->bindIndexBuffer(indexBuffer->getHandle());
      backend->bindVertexBuffer(vertexBuffer->getHandle());
      backend->drawIndexed(primitive.getIndexCount(), 1, primitive.getIndexOffset());
    }

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
    m_viewDescriptorSetLayout = backend.createDescriptorSetLayout(DescriptorSetLayoutBindings::perView());
  }

  if(!m_viewDescriptorSet) {
    m_viewDescriptorSet = backend.createDescriptorSet(m_viewDescriptorSetLayout);
    backend.updateDescriptorSetBuffer(m_viewDescriptorSet, m_viewUniformBufferHandle, 0);
  }

  if(!m_objectDescriptorSetLayout) {
    m_objectDescriptorSetLayout = backend.createDescriptorSetLayout(DescriptorSetLayoutBindings::perObject());
  }

  auto meshView = scene.view<Mesh>();
  size_t meshCount = meshView.size();

  // prepare per object buffer
  if(meshCount > perObjectUniformBufferData.size()) {
    perObjectUniformBufferData.resize(meshCount);

    if(m_objectsUniformBufferHandle) {
      backend.destroyBuffer(m_objectsUniformBufferHandle);
    }
    m_objectsUniformBufferHandle = backend.createBuffer(BufferBinding::UNIFORM, perObjectUniformBufferData.size() * sizeof(PerObjectUniforms));

    backend.destroyDescriptorSet(m_objectDescriptorSet);
    m_objectDescriptorSet = { };
  }

  if(!m_objectDescriptorSet) {
    m_objectDescriptorSet = backend.createDescriptorSet(m_objectDescriptorSetLayout);
    backend.updateDescriptorSetBuffer(m_objectDescriptorSet, m_objectsUniformBufferHandle, 0, 0, sizeof(PerObjectUniforms));
  }

  uint32_t index = 0;
  for(const auto& [entity, mesh] : meshView.each()) {
    const auto tr = scene.tryGet<Transform>(entity);

    auto& uniformBufferData = perObjectUniformBufferData[index];
    uniformBufferData.model = tr ? tr->transform : glm::mat4(1.0f);
    uniformBufferData.modelInverse = inverse(uniformBufferData.model);
    uniformBufferData.modelInverseTranspose = transpose(uniformBufferData.modelInverse);

    index++;

    for (auto& material : mesh.materials) {
      material->updateTextures(backend);
      material->updateBuffers(backend);
    }
  }

  backend.updateBuffer(m_viewUniformBufferHandle, &perViewUniformBufferData, sizeof(perViewUniformBufferData));
  backend.updateBuffer(m_objectsUniformBufferHandle, perObjectUniformBufferData.data(), perObjectUniformBufferData.size() * sizeof(PerObjectUniforms));
}

void Renderer::terminate(Engine& engine) {
  RenderAPI& backend = *engine.getRenderAPI();

  backend.destroyDescriptorSet(m_viewDescriptorSet);
  backend.destroyDescriptorSet(m_objectDescriptorSet);

  backend.destroyDescriptorSetLayout(m_viewDescriptorSetLayout);
  backend.destroyDescriptorSetLayout(m_objectDescriptorSetLayout);

  backend.destroyBuffer(m_viewUniformBufferHandle);
  backend.destroyBuffer(m_objectsUniformBufferHandle);
}

}
