#include "Renderer.h"

#include <iostream>
#include <ecs/Scene.h>

#include "app/System.h"

#include "Mesh.h"
#include "MaterialInstance.h"
#include "Skybox.h"
#include "ecs/Lights.h"
#include "ecs/SceneLighting.h"
#include "ecs/Transform.h"
#include "glm/gtc/constants.hpp"
#include <glm/gtc/matrix_transform.hpp>

#include "Renderable.h"
#include "Skin.h"
#include "ecs/AnimatorComponent.h"

namespace ailo {

static constexpr SamplerParams kClampToEdgeSampler {
  .wrapS = SamplerWrapMode::CLAMP_TO_EDGE,
  .wrapT = SamplerWrapMode::CLAMP_TO_EDGE,
  .wrapR = SamplerWrapMode::CLAMP_TO_EDGE,
};

// Hardware depth comparison; linear filtering blends the four nearest comparison results (2x2 PCF).
static constexpr SamplerParams kShadowSampler {
  .wrapS = SamplerWrapMode::CLAMP_TO_EDGE,
  .wrapT = SamplerWrapMode::CLAMP_TO_EDGE,
  .wrapR = SamplerWrapMode::CLAMP_TO_EDGE,
  .compareMode = SamplerCompareMode::COMPARE_TO_TEXTURE,
  .compareFunc = CompareOp::LESS_OR_EQUAL,
};

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

Renderer::Renderer(RenderAPI* renderApi, AssetServer& server, AssetStorage<Texture>& textures,
                   AssetStorage<Mesh>& meshes, AssetStorage<MaterialInstance>& materialInstances,
                   const RendererSettings& settings)
  : m_settings(settings), m_renderAPI(renderApi) {
  m_persistentAssets.push_back(createWhiteTexture(textures));
  m_persistentAssets.push_back(createBlackTexture(textures));
  m_persistentAssets.push_back(createDefaultMetallicRoughnessTexture(textures));
  m_persistentAssets.push_back(createDefaultNormalTexture(textures));
  m_defaultIblSpecular = createBlackCubemapTexture(textures);

  // vk::Format::eR32G32B32A32Sfloat
  m_iblDfgLut = server.load<Texture>(m_settings.dfgLutPath);

  auto backend = m_renderAPI;
  m_viewUniformBufferHandle = backend->createBuffer(BufferBinding::UNIFORM, sizeof(m_perViewUniformBufferData));
  m_lightsUniformBufferHandle = backend->createBuffer(BufferBinding::UNIFORM, sizeof(m_lightUniformsBufferData));
  m_viewDescriptorSetLayout = backend->createDescriptorSetLayout(DescriptorSetLayoutBindings::perView());
  m_objectDescriptorSetLayout = backend->createDescriptorSetLayout(DescriptorSetLayoutBindings::perObject());
  m_viewDescriptorSet = backend->createDescriptorSet(m_viewDescriptorSetLayout);

  backend->updateDescriptorSetBuffer(m_viewDescriptorSet, m_viewUniformBufferHandle, std::to_underlying(PerViewDescriptorBindings::FRAME_UNIFORMS));
  backend->updateDescriptorSetBuffer(m_viewDescriptorSet, m_lightsUniformBufferHandle, std::to_underlying(PerViewDescriptorBindings::LIGHTS));

  backend->updateDescriptorSetTexture(m_viewDescriptorSet, m_iblDfgLut->getHandle(), std::to_underlying(PerViewDescriptorBindings::IBL_DFG_LUT), kClampToEdgeSampler);
  m_iblSpecularMap = m_defaultIblSpecular->getHandle();
  backend->updateDescriptorSetTexture(m_viewDescriptorSet, m_iblSpecularMap, std::to_underlying(PerViewDescriptorBindings::IBL_SPECULAR_MAP));

  m_shadowViewUniformBufferHandle = backend->createBuffer(BufferBinding::UNIFORM, sizeof(m_shadowViewUniformBufferData));
  m_shadowViewDescriptorSet = backend->createDescriptorSet(m_viewDescriptorSetLayout);
  backend->updateDescriptorSetBuffer(m_shadowViewDescriptorSet, m_shadowViewUniformBufferHandle, std::to_underlying(PerViewDescriptorBindings::FRAME_UNIFORMS));
  backend->updateDescriptorSetBuffer(m_shadowViewDescriptorSet, m_lightsUniformBufferHandle, std::to_underlying(PerViewDescriptorBindings::LIGHTS));

  m_skyboxMaterial = MaterialInstance::create(materialInstances, server, *m_renderAPI, server.load<Material>(materials::kSkybox));
  m_skyboxMesh = Mesh::skyboxCube(meshes, m_renderAPI);

  m_dummyBonesBuffer = backend->createBuffer(BufferBinding::UNIFORM, sizeof(BonesUniform));
}

Renderer::~Renderer() = default;

void Renderer::render(Scene& scene, Query<Renderable> renderables, const ViewProjection& camera) {
  if (!beginFrame()) {
    return;
  }

  prepare(scene, renderables, camera);
  shadowPass();
  colorPass();

  for (auto& pass : m_overlayPasses) {
    pass();
  }

  endFrame();
}

void Renderer::addOverlayPass(OverlayPass pass) {
  m_overlayPasses.push_back(std::move(pass));
}

bool Renderer::beginFrame() {
  return m_renderAPI->beginFrame();
}

void Renderer::shadowPass() {
  RenderAPI* backend = m_renderAPI;

  if (!m_shadowMapTexture) {
    m_shadowMapTexture = backend->createTexture(
        TextureType::TEXTURE_2D, vk::Format::eD32Sfloat,
        TextureUsage::Sampled | TextureUsage::DepthStencilAttachment,
        m_settings.shadowMapSize, m_settings.shadowMapSize);

    backend->updateDescriptorSetTexture(m_viewDescriptorSet, m_shadowMapTexture, std::to_underlying(PerViewDescriptorBindings::SHADOW_MAP), kShadowSampler);
  }

  if (!m_shadowMapRenderTarget) {
    m_shadowMapRenderTarget = backend->createRenderTarget(
        {}, m_shadowMapTexture, m_settings.shadowMapSize, m_settings.shadowMapSize, vk::SampleCountFlagBits::e1);
  }

  RenderPassDescription shadowPassDesc {};
  shadowPassDesc.depth = { vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore };

  backend->beginRenderPass(m_shadowMapRenderTarget, shadowPassDesc);

  PipelineState pipelineState {};

  for(const RenderData& renderData : m_renderData) {
    if (!renderData.depthProgram) {
      continue;
    }

    pipelineState.program = renderData.depthProgram;
    pipelineState.vertexBufferLayout = renderData.vertexBufferLayout;
    backend->bindPipeline(pipelineState);

    backend->bindDescriptorSet(m_shadowViewDescriptorSet, std::to_underlying(DescriptorSetBindingPoints::PER_VIEW));
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

void Renderer::colorPass() {
  RenderAPI* backend = m_renderAPI;

  RenderPassDescription renderPass {};
  renderPass.color[0] = { .load = vk::AttachmentLoadOp::eClear, .store = vk::AttachmentStoreOp::eStore };
  renderPass.depth = { .load = vk::AttachmentLoadOp::eClear, .store = vk::AttachmentStoreOp::eDontCare };

  const auto& clearColor = m_settings.clearColor;
  backend->beginRenderPass(renderPass, vk::ClearColorValue(clearColor.r, clearColor.g, clearColor.b, clearColor.a));

  PipelineState pipelineState {};

  for(const RenderData& renderData : m_renderData) {
      if (!renderData.colorProgram) {
        continue;
      }

      pipelineState.program = renderData.colorProgram;
      pipelineState.vertexBufferLayout = renderData.vertexBufferLayout;

      backend->bindPipeline(pipelineState);

      backend->bindDescriptorSet(m_viewDescriptorSet, std::to_underlying(DescriptorSetBindingPoints::PER_VIEW));
      backend->bindDescriptorSet(
        renderData.objectDescriptorSet,
        std::to_underlying(DescriptorSetBindingPoints::PER_RENDERABLE),
        { renderData.objectBufferOffset, 0 });

      renderData.materialInstance->bind(*backend);

      backend->bindIndexBuffer(renderData.indexBuffer);
      backend->bindVertexBuffer(renderData.vertexBuffer);
      backend->drawIndexed(renderData.indexCount, 1, renderData.indexOffset);
  }

  drawSkybox();

  backend->endRenderPass();
}

void Renderer::drawSkybox() {
  if (!m_drawSkybox) {
    return;
  }

  auto program = m_skyboxMaterial->getMaterial().getProgram(material::Variant {});
  if (!program) {
    return;
  }

  RenderAPI* backend = m_renderAPI;
  backend->bindPipeline(PipelineState {
    .program = program,
    .vertexBufferLayout = m_skyboxMesh->vertexBuffer.getLayout(),
  });
  backend->bindDescriptorSet(m_viewDescriptorSet, std::to_underlying(DescriptorSetBindingPoints::PER_VIEW));
  backend->bindDescriptorSet(m_objectDescriptorSet, std::to_underlying(DescriptorSetBindingPoints::PER_RENDERABLE),
    { m_skyboxObjectBufferOffset, 0 });
  m_skyboxMaterial->bind(*backend);
  backend->bindIndexBuffer(m_skyboxMesh->indexBuffer.getHandle());
  backend->bindVertexBuffer(m_skyboxMesh->vertexBuffer.getBuffer());
  const auto& face = m_skyboxMesh->faces.front();
  backend->drawIndexed(face.indexCount, 1, face.indexOffset);
}

void Renderer::endFrame() {
  m_renderAPI->endFrame();
}

void Renderer::prepare(Scene& scene, Query<Renderable>& renderables, const ViewProjection& camera) {
  auto& backend = *m_renderAPI;

  const SceneLighting* sceneLighting = camera.lighting;
  glm::vec3 lightDir = sceneLighting ? sceneLighting->lightDirection : glm::vec3(0.0f, 1.0f, 0.0f);

  // Compute light view-projection matrix
  float extent = 22;
  float nearPlane = 0.1f;
  float farPlane = 32.0f;
  glm::vec3 center = glm::vec3(0.0f);
  glm::vec3 lightPos = center + lightDir * 18.0f;

  // lookAt degenerates when the light direction is parallel to up.
  glm::vec3 up = glm::abs(glm::dot(lightDir, glm::vec3(0, 1, 0))) > 0.99f
      ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
  glm::mat4 lightView = glm::lookAt(lightPos, center, up);
  glm::mat4 lightProjection = glm::ortho(-extent, extent, -extent, extent, nearPlane, farPlane);
  // Flip Y for Vulkan
  lightProjection[1][1] *= -1.0f;

  glm::mat4 lightVP = lightProjection * lightView;

  m_shadowViewUniformBufferData.projection = lightProjection;
  m_shadowViewUniformBufferData.view = lightView;
  m_shadowViewUniformBufferData.viewInverse = inverse(lightView);
  m_shadowViewUniformBufferData.lightDirection = lightDir;
  m_shadowViewUniformBufferData.lightViewProjection = lightVP;

  m_perViewUniformBufferData.projection = camera.projection;
  m_perViewUniformBufferData.view = camera.view;
  m_perViewUniformBufferData.viewInverse = inverse(camera.view);
  m_perViewUniformBufferData.lightColorIntensity = glm::vec4(1.0f, 1.0f, 1.0f, 1.2f);
  m_perViewUniformBufferData.lightDirection = lightDir;
  m_perViewUniformBufferData.ambientLightColorIntensity = glm::vec4(1.0f, 1.0f, 1.0f, 0.01f);
  const Texture& iblSpecular = sceneLighting && sceneLighting->prefilteredEnvMap
      ? *sceneLighting->prefilteredEnvMap : *m_defaultIblSpecular;
  m_perViewUniformBufferData.iblSpecularMaxLod = static_cast<float>(iblSpecular.getLevels() - 1);
  m_perViewUniformBufferData.lightViewProjection = lightVP;

  uint32_t lightCount = 0;

  for (auto&& [entity, light, tr] : scene.view<PointLight, TransformComponent>().each()) {
    if (lightCount == m_lightUniformsBufferData.size()) break;
    auto& uniform = m_lightUniformsBufferData[lightCount++];
    uniform.type = 0;
    uniform.lightPositionFalloff = glm::vec4(tr.world().position, 1.0f / (light.radius * light.radius));
    uniform.lightColorIntensity = glm::vec4(light.color, light.intensity);
  }

  for (auto&& [entity, light, tr] : scene.view<SpotLight, TransformComponent>().each()) {
    if (lightCount == m_lightUniformsBufferData.size()) break;
    const Transform& world = tr.world();
    auto& uniform = m_lightUniformsBufferData[lightCount++];
    uniform.type = 1;
    uniform.lightPositionFalloff = glm::vec4(world.position, 1.0f / (light.radius * light.radius));
    uniform.lightColorIntensity = glm::vec4(light.color, light.intensity);
    uniform.direction = world.rotation * glm::vec3(0.0f, 0.0f, -1.0f);
    uniform.scaleOffset = getSpotLightScaleOffset(light.innerAngle, light.outerAngle);
  }

  m_perViewUniformBufferData.lightCount = lightCount;

  const Skybox* skybox = camera.skybox;
  m_drawSkybox = skybox && skybox->cubemap;

  // One extra slot for the skybox's (identity) object uniforms.
  size_t meshCount = renderables.view().size() + 1;

  if(meshCount > m_perObjectUniformBufferData.size() || !m_objectsUniformBufferHandle) {
    m_perObjectUniformBufferData.resize(std::max(meshCount, m_perObjectUniformBufferData.size()));

    m_objectsUniformBufferHandle = backend.createBuffer(BufferBinding::UNIFORM, m_perObjectUniformBufferData.size() * sizeof(PerObjectUniforms));
    m_objectDescriptorSetDirty = true;
  }

  if (m_objectDescriptorSetDirty) {
    m_objectDescriptorSet = backend.createDescriptorSet(m_objectDescriptorSetLayout);
    backend.updateDescriptorSetBuffer(m_objectDescriptorSet, m_objectsUniformBufferHandle,
      std::to_underlying(PerObjectDescriptorBindings::OBJECT_UNIFORMS), 0, sizeof(PerObjectUniforms));

    backend.updateDescriptorSetBuffer(m_objectDescriptorSet, m_dummyBonesBuffer,
      std::to_underlying(PerObjectDescriptorBindings::BONE_UNIFORMS), 0, sizeof(BonesUniform));

    for (auto&& [entity, renderable] : renderables.each()) {
      renderable.descriptorSet.reset();
    }

    m_objectDescriptorSetDirty = false;
  }

  m_renderData.clear();
  m_renderData.reserve(meshCount * 2);

  uint32_t objectIndex = 0;
  for(const auto& [entity, renderable] : renderables.each()) {
    const auto tr = scene.tryGet<TransformComponent>(entity);
    auto skin = scene.tryGet<Skin>(entity);

    auto& uniformBufferData = m_perObjectUniformBufferData[objectIndex];
    uniformBufferData.model = tr ? tr->world().toMatrix() : glm::mat4(1.0f);
    uniformBufferData.modelInverse = inverse(uniformBufferData.model);
    uniformBufferData.modelInverseTranspose = transpose(uniformBufferData.modelInverse);
    uniformBufferData.flags = skin ? std::to_underlying(ObjectFlags::SkinningEnabled) : 0u;

    material::Variant variant {};
    if (skin) variant.key |= material::Variant::SKINNING;
    if (renderable.receiveShadows) variant.key |= material::Variant::SHADOWS;

    auto mesh = renderable.mesh;
    for(size_t i = 0; i < mesh->faces.size(); i++) {
      auto& [indexOffset, indexCount] = mesh->faces[i];
      auto& instance = renderable.materials[i];
      instance->commit(backend);

      const Material& material = instance->getMaterial();

      auto& entry = m_renderData.emplace_back();

      if (skin) {
        BufferHandle bones = m_dummyBonesBuffer;
        if (scene.isValid(skin->animator)) {
          auto animator = scene.tryGet<AnimatorComponent>(skin->animator);
          if (animator && animator->boneBuffer.getHandle()) {
            bones = animator->boneBuffer.getHandle();
          }
        }

        auto& objectDescriptor = renderable.descriptorSet;
        if (!objectDescriptor || renderable.descriptorSetBones != bones) {
          objectDescriptor = backend.createDescriptorSet(m_objectDescriptorSetLayout);
          renderable.descriptorSetBones = bones;

          backend.updateDescriptorSetBuffer(
          objectDescriptor, m_objectsUniformBufferHandle,
          std::to_underlying(PerObjectDescriptorBindings::OBJECT_UNIFORMS),
          0, sizeof(PerObjectUniforms));

          backend.updateDescriptorSetBuffer(
            objectDescriptor, bones,
            std::to_underlying(PerObjectDescriptorBindings::BONE_UNIFORMS),
            0, sizeof(BonesUniform));
        }

        entry.objectDescriptorSet = objectDescriptor;
      }
      else {
        entry.objectDescriptorSet = m_objectDescriptorSet;
      }

      entry.objectBufferOffset = objectIndex * sizeof(PerObjectUniforms);
      entry.colorProgram = material.getProgram(variant);
      entry.depthProgram = renderable.castShadows ? material.getProgram(material::Variant::depth(variant)) : ProgramHandle {};
      entry.vertexBufferLayout = mesh->vertexBuffer.getLayout();
      entry.materialInstance = instance.get();
      entry.indexBuffer = mesh->indexBuffer.getHandle();
      entry.vertexBuffer = mesh->vertexBuffer.getBuffer();
      entry.indexCount = indexCount;
      entry.indexOffset = indexOffset;
    }

    objectIndex++;
  }

  if (m_drawSkybox) {
    auto& uniformBufferData = m_perObjectUniformBufferData[objectIndex];
    uniformBufferData = PerObjectUniforms {};
    uniformBufferData.flags = 0;
    m_skyboxObjectBufferOffset = objectIndex * sizeof(PerObjectUniforms);
    objectIndex++;

    if (skybox->cubemap->getHandle() != m_skyboxTexture) {
      m_skyboxTexture = skybox->cubemap->getHandle();
      m_skyboxMaterial->setParameter("skybox", skybox->cubemap);
    }
    m_skyboxMaterial->commit(backend);
  }

  backend.updateBuffer(m_shadowViewUniformBufferHandle, &m_shadowViewUniformBufferData, sizeof(m_shadowViewUniformBufferData));
  backend.updateBuffer(m_viewUniformBufferHandle, &m_perViewUniformBufferData, sizeof(m_perViewUniformBufferData));
  backend.updateBuffer(m_lightsUniformBufferHandle, m_lightUniformsBufferData.data(), sizeof(m_lightUniformsBufferData));
  if (objectIndex > 0) {
    backend.updateBuffer(m_objectsUniformBufferHandle, m_perObjectUniformBufferData.data(), objectIndex * sizeof(PerObjectUniforms));
  }

  if (iblSpecular.getHandle() != m_iblSpecularMap) {
    m_iblSpecularMap = iblSpecular.getHandle();
    backend.updateDescriptorSetTexture(m_viewDescriptorSet, m_iblSpecularMap, std::to_underlying(PerViewDescriptorBindings::IBL_SPECULAR_MAP));
  }
}

AssetPtr<Texture> Renderer::createWhiteTexture(AssetStorage<Texture>& textures) {
  static const std::array<uint8_t, 4> white = { 255, 255, 255, 255 };

  auto texture = textures.emplace("builtin://textures/white", m_renderAPI, TextureType::TEXTURE_2D, vk::Format::eR8G8B8A8Srgb, TextureUsage::Sampled, 1, 1, 1);
  texture->updateImage(m_renderAPI, white.data(), 4);
  return texture;
}

AssetPtr<Texture> Renderer::createBlackTexture(AssetStorage<Texture>& textures) {
  static const std::array<uint8_t, 4> black = { 0, 0, 0, 255 };

  auto texture = textures.emplace("builtin://textures/black", m_renderAPI, TextureType::TEXTURE_2D, vk::Format::eR8G8B8A8Srgb, TextureUsage::Sampled, 1, 1, 1);
  texture->updateImage(m_renderAPI, black.data(), 4);
  return texture;
}

AssetPtr<Texture> Renderer::createDefaultNormalTexture(AssetStorage<Texture>& textures) {
  static const std::array<uint8_t, 4> normal = { 128, 128, 255, 255 };

  auto texture = textures.emplace("builtin://textures/normal@norm", m_renderAPI, TextureType::TEXTURE_2D, vk::Format::eR8G8B8A8Unorm, TextureUsage::Sampled, 1, 1, 1);
  texture->updateImage(m_renderAPI, normal.data(), 4);
  return texture;
}

AssetPtr<Texture> Renderer::createDefaultMetallicRoughnessTexture(AssetStorage<Texture>& textures) {
  static const std::array<uint8_t, 4> metallicRoughness = { 0, 128, 0, 255 };

  auto texture = textures.emplace("builtin://textures/default_metallic_roughness",
    m_renderAPI, TextureType::TEXTURE_2D, vk::Format::eR8G8B8A8Unorm, TextureUsage::Sampled, 1, 1, 1);
  texture->updateImage(m_renderAPI, metallicRoughness.data(), 4);
  return texture;
}

AssetPtr<Texture> Renderer::createBlackCubemapTexture(AssetStorage<Texture>& textures) {
  static const std::array<uint8_t, 4> black = { 0, 0, 0, 255 };

  auto texture = textures.emplace("builtin://textures/black_cube", m_renderAPI, TextureType::TEXTURE_CUBEMAP, vk::Format::eR8G8B8A8Srgb, TextureUsage::Sampled, 1, 1, 1);
  for (uint32_t face = 0; face < 6; face++) {
    texture->updateImage(m_renderAPI, black.data(), black.size(), 1, 1, 0, 0, face, 1);
  }
  return texture;
}

}
