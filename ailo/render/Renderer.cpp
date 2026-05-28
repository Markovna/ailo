#include "Renderer.h"

#include <iostream>
#include <ecs/Scene.h>

#include "Engine.h"
#include "Mesh.h"
#include "Shader.h"
#include "Material.h"
#include "ecs/SceneLighting.h"
#include "ecs/Transform.h"
#include "glm/gtc/constants.hpp"
#include <glm/gtc/matrix_transform.hpp>

#include "Renderable.h"
#include "Skin.h"

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

Renderer::Renderer(RenderAPI* renderApi, AssetManager* assetManager) : m_renderAPI(renderApi) {
  m_persistentAssets.push_back(asset_ptr_cast<Asset>(createWhiteTexture(assetManager)));
  m_persistentAssets.push_back(asset_ptr_cast<Asset>(createBlackTexture(assetManager)));
  m_persistentAssets.push_back(asset_ptr_cast<Asset>(createDefaultMetallicRoughnessTexture(assetManager)));
  m_persistentAssets.push_back(asset_ptr_cast<Asset>(createDefaultNormalTexture(assetManager)));

  // vk::Format::eR32G32B32A32Sfloat
  m_iblDfgLut = assetManager->load<Texture>("assets/textures/dfg_lut.hdr");

  auto backend = m_renderAPI;
  m_viewUniformBufferHandle = backend->createBuffer(BufferBinding::UNIFORM, sizeof(m_perViewUniformBufferData));
  m_lightsUniformBufferHandle = backend->createBuffer(BufferBinding::UNIFORM, sizeof(m_lightUniformsBufferData));
  m_viewDescriptorSetLayout = backend->createDescriptorSetLayout(DescriptorSetLayoutBindings::perView());
  m_objectDescriptorSetLayout = backend->createDescriptorSetLayout(DescriptorSetLayoutBindings::perObject());
  m_viewDescriptorSet = backend->createDescriptorSet(m_viewDescriptorSetLayout);
  m_objectDescriptorSet = backend->createDescriptorSet(m_objectDescriptorSetLayout);

  backend->updateDescriptorSetBuffer(m_viewDescriptorSet, m_viewUniformBufferHandle, std::to_underlying(PerViewDescriptorBindings::FRAME_UNIFORMS));
  backend->updateDescriptorSetBuffer(m_viewDescriptorSet, m_lightsUniformBufferHandle, std::to_underlying(PerViewDescriptorBindings::LIGHTS));

  backend->updateDescriptorSetTexture(m_viewDescriptorSet, m_iblDfgLut->getHandle(), std::to_underlying(PerViewDescriptorBindings::IBL_DFG_LUT));

  m_shadowShader = Shader::load(assetManager, m_renderAPI, Shader::getShadowShaderDescription());
  m_skinnedShadowShader = Shader::load(assetManager, m_renderAPI, Shader::getSkinnedShadowShaderDescription());

  // Provide a valid (all-identity) bone buffer for non-skinned entities so
  // the descriptor set binding is always satisfied.
  m_dummyBonesBuffer = backend->createBuffer(BufferBinding::UNIFORM, sizeof(BonesUniform));
  backend->updateDescriptorSetBuffer(m_objectDescriptorSet, m_dummyBonesBuffer,
      std::to_underlying(PerObjectDescriptorBindings::BONE_UNIFORMS),
      0, sizeof(BonesUniform));
}

Renderer::~Renderer() = default;

bool Renderer::beginFrame() {
  return m_renderAPI->beginFrame();
}

void Renderer::shadowPass(Scene& scene) {
  RenderAPI* backend = m_renderAPI;

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
  prepare(scene);

  // Begin depth-only render pass
  RenderPassDescription shadowPassDesc {};
  shadowPassDesc.depth = { vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore };

  backend->beginRenderPass(m_shadowMapRenderTarget, shadowPassDesc);

  PipelineState pipelineState {};

  for(const RenderData& renderData : m_renderData) {
    // Skip entities without Transform (e.g. skybox) — they shouldn't cast shadows
    if (!renderData.hasTransform) {
      continue;
    }

    pipelineState.program = renderData.isSkinned
        ? m_skinnedShadowShader->program()
        : m_shadowShader->program();
    pipelineState.vertexBufferLayout = renderData.vertexBufferLayout;
    backend->bindPipeline(pipelineState);

    backend->bindDescriptorSet(m_viewDescriptorSet, std::to_underlying(DescriptorSetBindingPoints::PER_VIEW));
    backend->bindDescriptorSet(
      renderData.objectDescriptorSet,
      std::to_underlying(DescriptorSetBindingPoints::PER_RENDERABLE),
      { renderData.objectBufferOffset, 0 });

    backend->bindIndexBuffer(renderData.indexBuffer);
    backend->bindVertexBuffer(renderData.vertexBuffer);

    backend->drawIndexed(renderData.indexCount, 1, renderData.indexOffset);
  }

  backend->endRenderPass();
}

void Renderer::colorPass(Scene& scene, const Camera& camera) {

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
  light0.lightPositionFalloff = glm::vec4(3.0, 1.5, 0.5f, 1.0f / (radius * radius));
  light0.lightColorIntensity = glm::vec4(1.0f, 1.0f, 0.0f, 1.0f);

  auto& light1 = m_lightUniformsBufferData[1];
  light1 = light0;
  light1.type = 1;
  light1.lightPositionFalloff = glm::vec4(.0, 1.5, 2.5f, 1.0f / (radius * radius));
  light1.lightColorIntensity = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
  light1.direction = glm::vec3(0.0f, 1.0f, 0.5f);
  light1.scaleOffset = getSpotLightScaleOffset(glm::radians(42.0), glm::radians(66.0));

  // prepare descriptor sets and uniform buffers
  prepare(scene);

  RenderAPI* backend = m_renderAPI;

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
      backend->bindDescriptorSet(
        renderData.objectDescriptorSet,
        std::to_underlying(DescriptorSetBindingPoints::PER_RENDERABLE),
        { renderData.objectBufferOffset, 0 });

      renderData.material->bindDescriptorSet(*backend);

      backend->bindIndexBuffer(renderData.indexBuffer);
      backend->bindVertexBuffer(renderData.vertexBuffer);
      backend->drawIndexed(renderData.indexCount, 1, renderData.indexOffset);
  }

  backend->endRenderPass();
}

void Renderer::endFrame() {
  m_renderAPI->endFrame();
}

void Renderer::onSceneCreated(Scene& scene) {
  scene.onDestroy<Renderable>().connect<&Renderer::onDestroyRenderable>(*this);
}

void Renderer::prepare(Scene& scene) {
  auto& backend = *m_renderAPI;

  auto renderableView = scene.view<Renderable>();
  size_t meshCount = renderableView.size();

  // prepare per object buffer
  if(meshCount > m_perObjectUniformBufferData.size() || !m_objectsUniformBufferHandle) {
    m_perObjectUniformBufferData.resize(std::max(meshCount, m_perObjectUniformBufferData.size()));

    if(m_objectsUniformBufferHandle) {
      backend.destroyBuffer(m_objectsUniformBufferHandle);
    }
    m_objectsUniformBufferHandle = backend.createBuffer(BufferBinding::UNIFORM, m_perObjectUniformBufferData.size() * sizeof(PerObjectUniforms));

    backend.updateDescriptorSetBuffer(m_objectDescriptorSet, m_objectsUniformBufferHandle, std::to_underlying(PerObjectDescriptorBindings::OBJECT_UNIFORMS), 0, sizeof(PerObjectUniforms));
  }

  m_renderData.clear();
  m_renderData.reserve(meshCount * 2);

  uint32_t index = 0;
  for(const auto& [entity, renderable] : renderableView.each()) {
    const auto tr = scene.tryGet<Transform>(entity);
    auto skin = scene.tryGet<Skin>(entity);

    auto& uniformBufferData = m_perObjectUniformBufferData[index];
    uniformBufferData.model = tr ? tr->transform : glm::mat4(1.0f);
    uniformBufferData.modelInverse = inverse(uniformBufferData.model);
    uniformBufferData.modelInverseTranspose = transpose(uniformBufferData.modelInverse);
    uniformBufferData.flags = skin ? std::to_underlying(ObjectFlags::SkinningEnabled) : 0u;

    auto mesh = renderable.mesh;
    for(size_t i = 0; i < mesh->faces.size(); i++) {
      auto& [indexOffset, indexCount] = mesh->faces[i];
      auto& material = renderable.materials[i];
      material->updateTextures(backend);
      material->updateBuffers(backend);

      auto& entry = m_renderData.emplace_back();

      if (skin) {
        auto& objectDescriptor = renderable.descriptorSet;
        if (!objectDescriptor) {
          objectDescriptor = backend.createDescriptorSet(m_objectDescriptorSetLayout);

          backend.updateDescriptorSetBuffer(
          objectDescriptor, m_objectsUniformBufferHandle,
          std::to_underlying(PerObjectDescriptorBindings::OBJECT_UNIFORMS),
          0, sizeof(PerObjectUniforms));

          backend.updateDescriptorSetBuffer(
            objectDescriptor, skin->getBuffer().getHandle(),
            std::to_underlying(PerObjectDescriptorBindings::BONE_UNIFORMS),
            0, sizeof(BonesUniform));
        }

        entry.objectDescriptorSet = objectDescriptor;
      }
      else {
        entry.objectDescriptorSet = m_objectDescriptorSet;
      }

      entry.objectBufferOffset = index * sizeof(PerObjectUniforms);
      entry.program = material->getShader()->program();
      entry.vertexBufferLayout = mesh->vertexBuffer->getLayout();
      entry.material = material.get();
      entry.indexBuffer = mesh->indexBuffer->getHandle();
      entry.vertexBuffer = mesh->vertexBuffer->getBuffer();
      entry.indexCount = indexCount;
      entry.indexOffset = indexOffset;
      entry.hasTransform = tr != nullptr;
      entry.isSkinned = (skin != nullptr);

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

void Renderer::onDestroyRenderable(entt::registry& registry, entt::entity entity) {
    Renderable& renderable = registry.get<Renderable>(entity);
    if (renderable.descriptorSet) {
        m_renderAPI->destroyDescriptorSet(renderable.descriptorSet);
    }
}

asset_ptr<Texture> Renderer::createWhiteTexture(AssetManager* assetManager) {
  static const std::array<uint8_t, 4> white = { 255, 255, 255, 255 };

  auto texture = assetManager->emplaceWithPath<Texture>("builtin://textures/white", m_renderAPI, TextureType::TEXTURE_2D, vk::Format::eR8G8B8A8Srgb, TextureUsage::Sampled, 1, 1, 1);
  texture->updateImage(m_renderAPI, white.data(), 4);
  return texture;
}

asset_ptr<Texture> Renderer::createBlackTexture(AssetManager* assetManager) {
  static const std::array<uint8_t, 4> black = { 0, 0, 0, 255 };

  auto texture = assetManager->emplaceWithPath<Texture>("builtin://textures/black", m_renderAPI, TextureType::TEXTURE_2D, vk::Format::eR8G8B8A8Srgb, TextureUsage::Sampled, 1, 1, 1);
  texture->updateImage(m_renderAPI, black.data(), 4);
  return texture;
}

asset_ptr<Texture> Renderer::createDefaultNormalTexture(AssetManager* assetManager) {
  static const std::array<uint8_t, 4> normal = { 128, 128, 255, 255 };

  auto texture = assetManager->emplaceWithPath<Texture>("builtin://textures/normal@norm", m_renderAPI, TextureType::TEXTURE_2D, vk::Format::eR8G8B8A8Unorm, TextureUsage::Sampled, 1, 1, 1);
  texture->updateImage(m_renderAPI, normal.data(), 4);
  return texture;
}

asset_ptr<Texture> Renderer::createDefaultMetallicRoughnessTexture(AssetManager* assetManager) {
  static const std::array<uint8_t, 4> metallicRoughness = { 0, 128, 0, 255 };

  auto texture = assetManager->emplaceWithPath<Texture>("builtin://textures/default_metallic_roughness",
    m_renderAPI, TextureType::TEXTURE_2D, vk::Format::eR8G8B8A8Srgb, TextureUsage::Sampled, 1, 1, 1);
  texture->updateImage(m_renderAPI, metallicRoughness.data(), 4);
  return texture;
}

void Renderer::terminate() {
  RenderAPI& backend = *m_renderAPI;

  backend.destroyDescriptorSet(m_viewDescriptorSet);
  backend.destroyDescriptorSet(m_objectDescriptorSet);

  backend.destroyDescriptorSetLayout(m_viewDescriptorSetLayout);
  backend.destroyDescriptorSetLayout(m_objectDescriptorSetLayout);

  backend.destroyBuffer(m_viewUniformBufferHandle);
  backend.destroyBuffer(m_lightsUniformBufferHandle);
  backend.destroyBuffer(m_objectsUniformBufferHandle);

  backend.destroyTexture(m_shadowMapTexture);
  backend.destroyRenderTarget(m_shadowMapRenderTarget);
  backend.destroyBuffer(m_dummyBonesBuffer);
}

}
