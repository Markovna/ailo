#include "Renderer.h"

#include <iostream>
#include <ecs/Scene.h>

#include "Engine.h"
#include "Mesh.h"
#include "Shader.h"
#include "ecs/SceneLighting.h"
#include "ecs/Transform.h"
#include "glm/gtc/constants.hpp"
#include <glm/gtc/matrix_transform.hpp>

#include "Renderable.h"

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

Renderer::Renderer(Engine& engine) {
  m_persistentTextures.push_back(createWhiteTexture(engine));
  m_persistentTextures.push_back(createBlackTexture(engine));
  m_persistentTextures.push_back(createDefaultMetallicRoughnessTexture(engine));
  m_persistentTextures.push_back(createDefaultNormalTexture(engine));

  m_iblDfgLut = Texture::load(engine, "assets/textures/dfg_lut.hdr", vk::Format::eR32G32B32A32Sfloat);

  auto backend = engine.getRenderAPI();
  m_viewUniformBufferHandle = backend->createBuffer(BufferBinding::UNIFORM, sizeof(m_perViewUniformBufferData));
  m_lightsUniformBufferHandle = backend->createBuffer(BufferBinding::UNIFORM, sizeof(m_lightUniformsBufferData));
  m_viewDescriptorSetLayout = backend->createDescriptorSetLayout(DescriptorSetLayoutBindings::perView());
  m_objectDescriptorSetLayout = backend->createDescriptorSetLayout(DescriptorSetLayoutBindings::perObject());
  m_viewDescriptorSet = backend->createDescriptorSet(m_viewDescriptorSetLayout);

  backend->updateDescriptorSetBuffer(m_viewDescriptorSet, m_viewUniformBufferHandle, std::to_underlying(PerViewDescriptorBindings::FRAME_UNIFORMS));
  backend->updateDescriptorSetBuffer(m_viewDescriptorSet, m_lightsUniformBufferHandle, std::to_underlying(PerViewDescriptorBindings::LIGHTS));

  backend->updateDescriptorSetTexture(m_viewDescriptorSet, m_iblDfgLut->getHandle(), std::to_underlying(PerViewDescriptorBindings::IBL_DFG_LUT));

  m_shadowShader = Shader::load(engine, Shader::getShadowShaderDescription());
}

Renderer::~Renderer() = default;

bool Renderer::beginFrame(Engine& engine) {
  RenderAPI* backend = engine.getRenderAPI();
  return backend->beginFrame();
}

void Renderer::shadowPass(Engine& engine, Scene& scene) {
  RenderAPI* backend = engine.getRenderAPI();

  // Create shadow map resources lazily
  if (!m_shadowMapTexture) {
    m_shadowMapTexture = backend->createTexture(
        TextureType::TEXTURE_2D, vk::Format::eD32Sfloat,
        TextureUsage::Sampled | TextureUsage::DepthStencilAttachment,
        kShadowMapSize, kShadowMapSize);

    backend->updateDescriptorSetTexture(m_viewDescriptorSet, m_shadowMapTexture, std::to_underlying(PerViewDescriptorBindings::SHADOW_MAP));
  }

  if (!m_shadowMapRenderTarget) {
    m_shadowMapRenderTarget = backend->createRenderTarget(
        {}, m_shadowMapTexture, kShadowMapSize, kShadowMapSize, vk::SampleCountFlagBits::e1);
  }

  auto sceneLighting = scene.tryGet<SceneLighting>(scene.single());

  // Compute light view-projection matrix
  glm::vec3 lightDir = sceneLighting ? sceneLighting->lightDirection : glm::vec3(0.0f, 1.0f, 0.0f);
  float extent = 22;
  float nearPlane = 0.1f;
  float farPlane = 32.0f;
  glm::vec3 center = glm::vec3(0.0f);
  glm::vec3 lightPos = center + lightDir * 18.0f;

  // glm::vec3 up = glm::abs(glm::dot(lightDir, glm::vec3(0, 1, 0))) > 0.99f
      // ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
  glm::vec3 up = glm::vec3(0, 1, 0);
  glm::mat4 lightView = glm::lookAt(lightPos, center, up);
  glm::mat4 lightProjection = glm::ortho(-extent, extent, -extent, extent, nearPlane, farPlane);
  // Flip Y for Vulkan
  lightProjection[1][1] *= -1.0f;

  glm::mat4 lightVP = lightProjection * lightView;
  m_perViewUniformBufferData.lightViewProjection = lightVP;
  m_perViewUniformBufferData.projection = lightProjection;
  m_perViewUniformBufferData.view = lightView;
  m_perViewUniformBufferData.viewInverse = inverse(lightView);

  // prepare descriptor sets and uniform buffers
  prepare(engine, scene);

  // Begin depth-only render pass
  RenderPassDescription shadowPassDesc {};
  shadowPassDesc.depth = { vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore };

  backend->beginRenderPass(m_shadowMapRenderTarget, shadowPassDesc);

  PipelineState pipelineState {};
  pipelineState.program = m_shadowShader->program();

  for(const RenderData& renderData : m_renderData) {
    // Skip entities without Transform (e.g. skybox) — they shouldn't cast shadows
    if (!renderData.hasTransform) {
      continue;
    }

    pipelineState.vertexBufferLayout = renderData.vertexBufferLayout;
    backend->bindPipeline(pipelineState);

    backend->bindDescriptorSet(m_viewDescriptorSet, std::to_underlying(DescriptorSetBindingPoints::PER_VIEW));
    backend->bindDescriptorSet(m_objectDescriptorSet, std::to_underlying(DescriptorSetBindingPoints::PER_RENDERABLE), { renderData.bufferOffset });

    backend->bindIndexBuffer(renderData.indexBuffer);
    backend->bindVertexBuffer(renderData.vertexBuffer);

    backend->drawIndexed(renderData.indexCount, 1, renderData.indexOffset);
  }

  backend->endRenderPass();
}

void Renderer::colorPass(Engine& engine, Scene& scene, const Camera& camera) {

  auto sceneLighting = scene.tryGet<SceneLighting>(scene.single());

  // prepare per view buffer
  m_perViewUniformBufferData.projection = camera.projection;
  m_perViewUniformBufferData.view = camera.view;
  m_perViewUniformBufferData.viewInverse = inverse(camera.view);
  m_perViewUniformBufferData.lightColorIntensity = glm::vec4(1.0f, 1.0f, 1.0f, 1.2f);
  m_perViewUniformBufferData.lightDirection = sceneLighting ? sceneLighting->lightDirection : glm::vec3(0.0f, 1.0f, 0.0f);
  m_perViewUniformBufferData.ambientLightColorIntensity = glm::vec4(1.0f, 1.0f, 1.0f, 0.01f);
  m_perViewUniformBufferData.iblSpecularMaxLod = sceneLighting ? sceneLighting->prefilteredEnvMap->getLevels() - 1 : 1;

  float radius = 3.0f;
  auto& light0 = m_lightUniformsBufferData[0];
  light0.type = 0; // 0 - point, 1 - spot
  light0.lightPositionFalloff = glm::vec4(1.0, 1.5, 0.5f, 1.0f / (radius * radius));
  light0.lightColorIntensity = glm::vec4(1.0f, 1.0f, 0.0f, 1.0f);

  auto& light1 = m_lightUniformsBufferData[1];
  light1 = light0;
  light1.type = 1;
  light1.lightPositionFalloff = glm::vec4(.0, 1.5, 2.5f, 1.0f / (radius * radius));
  light1.lightColorIntensity = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
  light1.direction = glm::vec3(0.0f, 1.0f, 0.5f);
  light1.scaleOffset = getSpotLightScaleOffset(glm::radians(42.0), glm::radians(66.0));

  // prepare descriptor sets and uniform buffers
  prepare(engine, scene);

  RenderAPI* backend = engine.getRenderAPI();

  RenderPassDescription renderPass {};
  renderPass.color[0] = { vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore };
  renderPass.depth = { vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare };

  backend->beginRenderPass(renderPass, vk::ClearColorValue(0.1f, 0.1f, 0.3f, 1.0f));

  PipelineState pipelineState {};

  for(const RenderData& renderData : m_renderData) {
      pipelineState.program = renderData.program;
      pipelineState.vertexBufferLayout = renderData.vertexBufferLayout;

      backend->bindPipeline(pipelineState);

      backend->bindDescriptorSet(m_viewDescriptorSet, std::to_underlying(DescriptorSetBindingPoints::PER_VIEW));
      backend->bindDescriptorSet(m_objectDescriptorSet, std::to_underlying(DescriptorSetBindingPoints::PER_RENDERABLE), { renderData.bufferOffset });

      renderData.material->bindDescriptorSet(*backend);

      backend->bindIndexBuffer(renderData.indexBuffer);
      backend->bindVertexBuffer(renderData.vertexBuffer);
      backend->drawIndexed(renderData.indexCount, 1, renderData.indexOffset);
  }

  backend->endRenderPass();
}

void Renderer::endFrame(Engine& engine) {
  RenderAPI* backend = engine.getRenderAPI();
  backend->endFrame();
}

void Renderer::prepare(Engine& engine, Scene& scene) {
  auto& backend = *engine.getRenderAPI();

  auto renderableView = scene.view<Renderable>();
  size_t meshCount = renderableView.size();

  // prepare per object buffer
  if(meshCount > m_perObjectUniformBufferData.size() || !m_objectsUniformBufferHandle) {
    m_perObjectUniformBufferData.resize(std::max(meshCount, m_perObjectUniformBufferData.size()));

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

  m_renderData.clear();
  m_renderData.reserve(meshCount * 2);

  uint32_t index = 0;
  for(const auto& [entity, renderable] : renderableView.each()) {
    const auto tr = scene.tryGet<Transform>(entity);

    auto& uniformBufferData = m_perObjectUniformBufferData[index];
    uniformBufferData.model = tr ? tr->transform : glm::mat4(1.0f);
    uniformBufferData.modelInverse = inverse(uniformBufferData.model);
    uniformBufferData.modelInverseTranspose = transpose(uniformBufferData.modelInverse);

    for(size_t i = 0; i < renderable.mesh->faces.size(); i++) {
      auto& face = renderable.mesh->faces[i];
      auto& material = renderable.materials[i];
      material->updateTextures(backend);
      material->updateBuffers(backend);

      m_renderData.push_back({});

      auto& entry = m_renderData.back();
      entry.program = material->getShader()->program();
      entry.vertexBufferLayout = renderable.mesh->vertexBuffer->getLayout();
      entry.bufferOffset = index * sizeof(PerObjectUniforms);
      entry.material = material.get();
      entry.indexBuffer = renderable.mesh->indexBuffer->getHandle();
      entry.vertexBuffer = renderable.mesh->vertexBuffer->getBuffer();
      entry.indexCount = face.indexCount;
      entry.indexOffset = face.indexOffset;
      entry.hasTransform = tr != nullptr;

      index++;
    }
  }

  backend.updateBuffer(m_viewUniformBufferHandle, &m_perViewUniformBufferData, sizeof(m_perViewUniformBufferData));
  backend.updateBuffer(m_lightsUniformBufferHandle, m_lightUniformsBufferData.data(), sizeof(m_lightUniformsBufferData));
  backend.updateBuffer(m_objectsUniformBufferHandle, m_perObjectUniformBufferData.data(), m_perObjectUniformBufferData.size() * sizeof(PerObjectUniforms));

  auto ibl = scene.tryGet<SceneLighting>(scene.single());
  auto iblTexHandle = ibl ? ibl->prefilteredEnvMap->getHandle() : TextureHandle{};
  if (iblTexHandle != m_iblSpecularMap) {
    m_iblSpecularMap = iblTexHandle;
    backend.updateDescriptorSetTexture(m_viewDescriptorSet, m_iblSpecularMap, std::to_underlying(PerViewDescriptorBindings::IBL_SPECULAR_MAP));
  }
}

asset_ptr<Texture> Renderer::createWhiteTexture(Engine& engine) {
  static const std::array<uint8_t, 4> white = { 255, 255, 255, 255 };

  auto texture = engine.getAssetManager()->emplace<Texture>("builtin://textures/white", engine, TextureType::TEXTURE_2D, vk::Format::eR8G8B8A8Srgb, TextureUsage::Sampled, 1, 1, 1);
  texture->updateImage(engine, white.data(), 4);
  return texture;
}

asset_ptr<Texture> Renderer::createBlackTexture(Engine& engine) {
  static const std::array<uint8_t, 4> black = { 0, 0, 0, 255 };

  auto texture = engine.getAssetManager()->emplace<Texture>("builtin://textures/black", engine, TextureType::TEXTURE_2D, vk::Format::eR8G8B8A8Srgb, TextureUsage::Sampled, 1, 1, 1);
  texture->updateImage(engine, black.data(), 4);
  return texture;
}

asset_ptr<Texture> Renderer::createDefaultNormalTexture(Engine& engine) {
  static const std::array<uint8_t, 4> normal = { 128, 128, 255, 255 };

  auto texture = engine.getAssetManager()->emplace<Texture>("builtin://textures/normal", engine, TextureType::TEXTURE_2D, vk::Format::eR8G8B8A8Unorm, TextureUsage::Sampled, 1, 1, 1);
  texture->updateImage(engine, normal.data(), 4);
  return texture;
}

asset_ptr<Texture> Renderer::createDefaultMetallicRoughnessTexture(Engine& engine) {
  static const std::array<uint8_t, 4> metallicRoughness = { 0, 128, 0, 255 };

  auto texture = engine.getAssetManager()->emplace<Texture>("builtin://textures/default_metallic_roughness",
    engine, TextureType::TEXTURE_2D, vk::Format::eR8G8B8A8Srgb, TextureUsage::Sampled, 1, 1, 1);
  texture->updateImage(engine, metallicRoughness.data(), 4);
  return texture;
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

  backend.destroyTexture(m_shadowMapTexture);
  backend.destroyRenderTarget(m_shadowMapRenderTarget);
}

}
