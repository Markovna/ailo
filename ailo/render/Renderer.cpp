#include "Renderer.h"
#include <ecs/Scene.h>

#include "Mesh.h"
#include "Shader.h"
#include "ecs/Transform.h"
#include "glm/gtc/constants.hpp"

namespace ailo {

static glm::vec2 getSpotLightScaleOffset(float inner, float outer) {
  float const outerClamped = std::clamp(std::abs(outer), glm::radians(0.5f), glm::half_pi<float>());
  float innerClamped = std::clamp(std::abs(inner), glm::radians(0.5f), glm::half_pi<float>());
  innerClamped = std::min(innerClamped, outerClamped);

  float const cosOuter = glm::cos(outerClamped);
  float const cosInner = glm::cos(innerClamped);
  float const scale = 1.0f / std::max(1.0f / 1024.0f, cosInner - cosOuter);
  float const offset = -cosOuter * scale;

  return { scale, offset };
}

bool Renderer::beginFrame(Engine& engine) {
  RenderAPI* backend = engine.getRenderAPI();
  return backend->beginFrame();
}

void Renderer::colorPass(Engine& engine, Scene& scene, const Camera& camera) {
  // prepare per view buffer
  m_perViewUniformBufferData.projection = camera.projection;
  m_perViewUniformBufferData.view = camera.view;
  m_perViewUniformBufferData.viewInverse = inverse(camera.view);
  m_perViewUniformBufferData.lightColorIntensity = glm::vec4(1.0f, 1.0f, 1.0f, 0.7f);
  m_perViewUniformBufferData.lightDirection = normalize(glm::vec3(1.0f, 5.0f, -3.0f));
  m_perViewUniformBufferData.ambientLightColorIntensity = glm::vec4(1.0f, 1.0f, 1.0f, 0.02f);

  // prepare lights data
  m_lightUniformsBufferData.lightPositionRadius = glm::vec4(40.0f, 100.0f, 0.0f, 70);
  m_lightUniformsBufferData.lightColorIntensity = glm::vec4(1.0f, 0.3f, 0.3f, 2.5f);
  m_lightUniformsBufferData.direction = glm::vec3(0.0f, 1.0f, 0.0f);
  m_lightUniformsBufferData.type = 0;
  m_lightUniformsBufferData.scaleOffset = getSpotLightScaleOffset(glm::radians(25.0), glm::radians(29.0));

  RenderAPI* backend = engine.getRenderAPI();

  // prepare descriptor sets and uniform buffers
  prepare(*backend, scene);

  RenderPassDescription renderPass {};
  renderPass.loadOp[0] = vk::AttachmentLoadOp::eClear;
  renderPass.storeOp[0] = vk::AttachmentStoreOp::eStore;

  renderPass.loadOp[kMaxColorAttachments] = vk::AttachmentLoadOp::eClear;
  renderPass.storeOp[kMaxColorAttachments] = vk::AttachmentStoreOp::eDontCare;

  backend->beginRenderPass(renderPass);

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
}

void Renderer::endFrame(Engine& engine) {
  RenderAPI* backend = engine.getRenderAPI();
  backend->endFrame();
}

void Renderer::render(Engine& engine, Scene& scene, const Camera& camera) {
  // prepare per view buffer
  m_perViewUniformBufferData.projection = camera.projection;
  m_perViewUniformBufferData.view = camera.view;
  m_perViewUniformBufferData.viewInverse = inverse(camera.view);
  m_perViewUniformBufferData.lightColorIntensity = glm::vec4(1.0f, 1.0f, 1.0f, 0.7f);
  m_perViewUniformBufferData.lightDirection = normalize(glm::vec3(1.0f, 5.0f, -3.0f));
  m_perViewUniformBufferData.ambientLightColorIntensity = glm::vec4(1.0f, 1.0f, 1.0f, 0.02f);

  // prepare lights data
  m_lightUniformsBufferData.lightPositionRadius = glm::vec4(40.0f, 100.0f, 0.0f, 70);
  m_lightUniformsBufferData.lightColorIntensity = glm::vec4(1.0f, 0.3f, 0.3f, 2.5f);
  m_lightUniformsBufferData.direction = glm::vec3(0.0f, 1.0f, 0.0f);
  m_lightUniformsBufferData.type = 0;
  m_lightUniformsBufferData.scaleOffset = getSpotLightScaleOffset(glm::radians(25.0), glm::radians(29.0));

  RenderAPI* backend = engine.getRenderAPI();
  backend->beginFrame();

  // prepare descriptor sets and uniform buffers
  prepare(*backend, scene);

  RenderPassDescription renderPass {};
  renderPass.loadOp[0] = vk::AttachmentLoadOp::eClear;
  renderPass.storeOp[0] = vk::AttachmentStoreOp::eStore;

  renderPass.loadOp[kMaxColorAttachments] = vk::AttachmentLoadOp::eClear;
  renderPass.storeOp[kMaxColorAttachments] = vk::AttachmentStoreOp::eDontCare;

  backend->beginRenderPass(renderPass);

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
    m_viewUniformBufferHandle = backend.createBuffer(BufferBinding::UNIFORM, sizeof(m_perViewUniformBufferData));
  }
  if(!m_lightsUniformBufferHandle) {
    m_lightsUniformBufferHandle = backend.createBuffer(BufferBinding::UNIFORM, sizeof(m_lightUniformsBufferData));
  }

  if(!m_viewDescriptorSetLayout) {
    m_viewDescriptorSetLayout = backend.createDescriptorSetLayout(DescriptorSetLayoutBindings::perView());
  }

  if(!m_viewDescriptorSet) {
    m_viewDescriptorSet = backend.createDescriptorSet(m_viewDescriptorSetLayout);
    backend.updateDescriptorSetBuffer(m_viewDescriptorSet, m_viewUniformBufferHandle, std::to_underlying(PerViewDescriptorBindings::FRAME_UNIFORMS));
    backend.updateDescriptorSetBuffer(m_viewDescriptorSet, m_lightsUniformBufferHandle, std::to_underlying(PerViewDescriptorBindings::LIGHTS));
  }

  if(!m_objectDescriptorSetLayout) {
    m_objectDescriptorSetLayout = backend.createDescriptorSetLayout(DescriptorSetLayoutBindings::perObject());
  }

  auto meshView = scene.view<Mesh>();
  size_t meshCount = meshView.size();

  // prepare per object buffer
  if(meshCount > m_perObjectUniformBufferData.size()) {
    m_perObjectUniformBufferData.resize(meshCount);

    if(m_objectsUniformBufferHandle) {
      backend.destroyBuffer(m_objectsUniformBufferHandle);
    }
    m_objectsUniformBufferHandle = backend.createBuffer(BufferBinding::UNIFORM, m_perObjectUniformBufferData.size() * sizeof(PerObjectUniforms));

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

    auto& uniformBufferData = m_perObjectUniformBufferData[index];
    uniformBufferData.model = tr ? tr->transform : glm::mat4(1.0f);
    uniformBufferData.modelInverse = inverse(uniformBufferData.model);
    uniformBufferData.modelInverseTranspose = transpose(uniformBufferData.modelInverse);

    index++;

    for (auto& material : mesh.materials) {
      material->updateTextures(backend);
      material->updateBuffers(backend);
    }
  }

  backend.updateBuffer(m_viewUniformBufferHandle, &m_perViewUniformBufferData, sizeof(m_perViewUniformBufferData));
  backend.updateBuffer(m_lightsUniformBufferHandle, &m_lightUniformsBufferData, sizeof(m_lightUniformsBufferData));
  backend.updateBuffer(m_objectsUniformBufferHandle, m_perObjectUniformBufferData.data(), m_perObjectUniformBufferData.size() * sizeof(PerObjectUniforms));
}

void Renderer::terminate(Engine& engine) {
  RenderAPI& backend = *engine.getRenderAPI();

  backend.destroyDescriptorSet(m_viewDescriptorSet);
  backend.destroyDescriptorSet(m_objectDescriptorSet);

  backend.destroyDescriptorSetLayout(m_viewDescriptorSetLayout);
  backend.destroyDescriptorSetLayout(m_objectDescriptorSetLayout);

  backend.destroyBuffer(m_viewUniformBufferHandle);
  backend.destroyBuffer(m_lightsUniformBufferHandle);
  backend.destroyBuffer(m_objectsUniformBufferHandle);
}

}
