#include "Renderer.h"

#include <ecs/Scene.h>

#include "app/System.h"

#include "DefaultAssetFactory.h"
#include "Mesh.h"
#include "embedded/shaders.h"
#include "MaterialInstance.h"
#include "Skybox.h"
#include "utils/Log.h"
#include "ecs/Lights.h"
#include "ecs/SceneLighting.h"
#include "ecs/Transform.h"
#include "glm/gtc/constants.hpp"
#include <glm/gtc/matrix_transform.hpp>

#include "Renderable.h"
#include "ShadowMapManager.h"
#include "Skin.h"
#include "ecs/AnimatorComponent.h"
#include "ecs/Transform.h"

namespace ailo {

static constexpr SamplerParams kClampToEdgeSampler {
  .wrapS = SamplerWrapMode::CLAMP_TO_EDGE,
  .wrapT = SamplerWrapMode::CLAMP_TO_EDGE,
  .wrapR = SamplerWrapMode::CLAMP_TO_EDGE,
};

struct TonemapUniforms {
  float exposure;
  float padding[3];
};

// Hardware depth comparison; linear filtering blends the four nearest comparison results (2x2 PCF).
static constexpr SamplerParams kShadowSampler {
  .wrapS = SamplerWrapMode::CLAMP_TO_EDGE,
  .wrapT = SamplerWrapMode::CLAMP_TO_EDGE,
  .wrapR = SamplerWrapMode::CLAMP_TO_EDGE,
  .compareMode = SamplerCompareMode::COMPARE_TO_TEXTURE,
  .compareFunc = CompareOp::LessEqual,
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

Renderer::Renderer(RenderAPI* renderApi, AssetServer& server, AssetStorage<MaterialInstance>& materialInstances, const RendererSettings& settings)
  : m_settings(settings), m_shadowMapManager(), m_renderAPI(renderApi) {
  m_defaultIblSpecular = server.load<Texture>(textures::kBlackCube);

  m_iblDfgLut = server.load<Texture>(textures::kDfgLut);

  auto backend = m_renderAPI;
  m_viewUniformBufferHandle = backend->createBuffer(BufferBinding::UNIFORM, sizeof(m_perViewUniformBufferData));
  m_shadowsUniformBufferHandle = backend->createBuffer(BufferBinding::UNIFORM, sizeof(m_shadowsUniformsBufferData));
  m_lightsUniformBufferHandle = backend->createBuffer(BufferBinding::UNIFORM, sizeof(m_lightUniformsBufferData));
  m_viewDescriptorSetLayout = backend->createDescriptorSetLayout(DescriptorSetLayoutBindings::perView());
  m_objectDescriptorSetLayout = backend->createDescriptorSetLayout(DescriptorSetLayoutBindings::perObject());
  m_viewDescriptorSet = backend->createDescriptorSet(m_viewDescriptorSetLayout);

  backend->updateDescriptorSetBuffer(m_viewDescriptorSet, m_viewUniformBufferHandle, std::to_underlying(PerViewDescriptorBindings::FRAME_UNIFORMS));
  backend->updateDescriptorSetBuffer(m_viewDescriptorSet, m_shadowsUniformBufferHandle, std::to_underlying(PerViewDescriptorBindings::SHADOWS));
  backend->updateDescriptorSetBuffer(m_viewDescriptorSet, m_lightsUniformBufferHandle, std::to_underlying(PerViewDescriptorBindings::LIGHTS));

  backend->updateDescriptorSetTexture(m_viewDescriptorSet, m_iblDfgLut->getHandle(), std::to_underlying(PerViewDescriptorBindings::IBL_DFG_LUT), kClampToEdgeSampler);
  m_iblSpecularMap = m_defaultIblSpecular->getHandle();
  backend->updateDescriptorSetTexture(m_viewDescriptorSet, m_iblSpecularMap, std::to_underlying(PerViewDescriptorBindings::IBL_SPECULAR_MAP));

  for (size_t i = 0; i < kMaxShadowCascades; i++) {
    auto& buffer = m_shadowViewUniformBufferHandles[i];
    auto& descriptorSet = m_shadowViewDescriptorSets[i];
    buffer = backend->createBuffer(BufferBinding::UNIFORM, sizeof(PerViewUniforms));
    descriptorSet = backend->createDescriptorSet(m_viewDescriptorSetLayout);
    backend->updateDescriptorSetBuffer(descriptorSet, buffer, std::to_underlying(PerViewDescriptorBindings::FRAME_UNIFORMS));
    backend->updateDescriptorSetBuffer(descriptorSet, m_shadowsUniformBufferHandle, std::to_underlying(PerViewDescriptorBindings::SHADOWS));
    backend->updateDescriptorSetBuffer(descriptorSet, m_lightsUniformBufferHandle, std::to_underlying(PerViewDescriptorBindings::LIGHTS));
  }

  m_skyboxMaterial = MaterialInstance::create(materialInstances, server, *m_renderAPI, server.load<Material>(materials::kSkybox));
  m_skyboxMesh = server.load<Mesh>(meshes::kSkyboxCube);

  m_dummyBonesBuffer = backend->createBuffer(BufferBinding::UNIFORM, sizeof(BonesUniform));

  const std::vector<DescriptorSetLayoutBinding> tonemapLayout {
    { .binding = 0, .descriptorType = vk::DescriptorType::eCombinedImageSampler, .stageFlags = vk::ShaderStageFlagBits::eFragment },
    { .binding = 1, .descriptorType = vk::DescriptorType::eUniformBuffer, .stageFlags = vk::ShaderStageFlagBits::eFragment },
  };
  m_tonemapDescriptorSetLayout = backend->createDescriptorSetLayout(tonemapLayout);
  m_tonemapDescriptorSet = backend->createDescriptorSet(m_tonemapDescriptorSetLayout);
  m_tonemapUniformBuffer = backend->createBuffer(BufferBinding::UNIFORM, sizeof(TonemapUniforms));
  backend->updateDescriptorSetBuffer(m_tonemapDescriptorSet, m_tonemapUniformBuffer, 1);
  m_tonemapProgram = backend->createProgram(ShaderDescription {
    .vertexShader = ShaderDescription::code(embedded::shaders::kTonemapVert),
    .fragmentShader = ShaderDescription::code(embedded::shaders::kTonemapFrag),
    .raster = RasterDescription {
      .cullingMode = CullingMode::None,
      .depthWriteEnable = false,
      .depthCompareOp = CompareOp::Always,
    },
    .layout = { tonemapLayout },
  });
}

Renderer::~Renderer() = default;

void Renderer::render(Scene& scene, Query<Renderable> renderables, const ViewInfo& camera) {
  if (!beginFrame()) {
    return;
  }

  prepare(scene, renderables, camera);
  shadowPass();
  colorPass();
  tonemapPass();

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

  const uint32_t tileSize = m_settings.shadowMapSize;
  const uint32_t atlasSize = tileSize * kShadowAtlasTiles;

  if (!m_shadowMapTexture) {
    m_shadowMapTexture = backend->createTexture(
        TextureType::Texture2D, vk::Format::eD32Sfloat,
        TextureUsage::Sampled | TextureUsage::DepthStencilAttachment,
        atlasSize, atlasSize);

    backend->updateDescriptorSetTexture(m_viewDescriptorSet, m_shadowMapTexture, std::to_underlying(PerViewDescriptorBindings::SHADOW_MAP), kShadowSampler);
  }

  if (!m_shadowMapRenderTarget) {
    m_shadowMapRenderTarget = backend->createRenderTarget(
        {}, m_shadowMapTexture, atlasSize, atlasSize, vk::SampleCountFlagBits::e1);
  }

  RenderPassDescription shadowPassDesc {};
  shadowPassDesc.depth = { vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore };

  backend->beginRenderPass(m_shadowMapRenderTarget, shadowPassDesc);

  PipelineState pipelineState {};

  for (uint32_t cascade = 0; cascade < m_shadowsUniformsBufferData.cascadeCount; cascade++) {
    const int32_t x = static_cast<int32_t>(cascade % kShadowAtlasTiles * tileSize);
    const int32_t y = static_cast<int32_t>(cascade / kShadowAtlasTiles * tileSize);
    backend->setViewport(static_cast<float>(x), static_cast<float>(y), static_cast<float>(tileSize), static_cast<float>(tileSize));
    backend->setScissor(x, y, tileSize, tileSize);

    for (const RenderData& renderData : m_renderData) {
      if (!renderData.depthProgram) {
        continue;
      }

      pipelineState.program = renderData.depthProgram;
      pipelineState.vertexBufferLayout = renderData.vertexBufferLayout;
      backend->bindPipeline(pipelineState);

      backend->bindDescriptorSet(m_shadowViewDescriptorSets[cascade], std::to_underlying(DescriptorSetBindingPoints::PER_VIEW));
      backend->bindDescriptorSet(
        renderData.objectDescriptorSet,
        std::to_underlying(DescriptorSetBindingPoints::PER_RENDERABLE),
        { renderData.objectBufferOffset, 0 });

      backend->bindIndexBuffer(renderData.indexBuffer, renderData.indexType);
      backend->bindVertexBuffer(renderData.vertexBuffer);

      backend->drawIndexed(renderData.indexCount, 1, renderData.indexOffset);
    }
  }

  backend->endRenderPass();
}

void Renderer::colorPass() {
  RenderAPI* backend = m_renderAPI;

  RenderPassDescription renderPass {};
  renderPass.color[0] = { .load = vk::AttachmentLoadOp::eClear, .store = vk::AttachmentStoreOp::eStore };
  renderPass.depth = { .load = vk::AttachmentLoadOp::eClear, .store = vk::AttachmentStoreOp::eDontCare };

  updateHdrTarget();
  const auto& clearColor = m_settings.clearColor;
  backend->beginRenderPass(m_hdrRenderTarget, renderPass,
    vk::ClearColorValue(clearColor.r, clearColor.g, clearColor.b, clearColor.a));

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

      backend->bindIndexBuffer(renderData.indexBuffer, renderData.indexType);
      backend->bindVertexBuffer(renderData.vertexBuffer);
      backend->drawIndexed(renderData.indexCount, 1, renderData.indexOffset);
  }

  drawSkybox();

  backend->endRenderPass();
}

void Renderer::tonemapPass() {
  RenderAPI* backend = m_renderAPI;

  // Every pixel is written, so the swapchain image isn't cleared.
  RenderPassDescription renderPass {};
  renderPass.color[0] = { .load = vk::AttachmentLoadOp::eDontCare, .store = vk::AttachmentStoreOp::eStore };
  backend->beginRenderPass(renderPass);

  backend->bindPipeline(PipelineState { .program = m_tonemapProgram });
  backend->bindDescriptorSet(m_tonemapDescriptorSet, 0);
  backend->draw(3);

  backend->endRenderPass();
}

void Renderer::updateHdrTarget() {
  RenderAPI* backend = m_renderAPI;

  const vk::Extent2D extent = backend->getSwapchainExtent();
  if (m_hdrRenderTarget && extent == m_hdrExtent) {
    return;
  }
  m_hdrExtent = extent;

  m_hdrColor = backend->createTexture(TextureType::Texture2D, vk::Format::eR16G16B16A16Sfloat,
    TextureUsage::ColorAttachment | TextureUsage::Sampled, extent.width, extent.height);
  m_hdrDepth = backend->createTexture(TextureType::Texture2D, vk::Format::eD32Sfloat,
    TextureUsage::DepthStencilAttachment, extent.width, extent.height);

  PerColorAttachment<TextureHandle> colors {};
  colors[0] = m_hdrColor;
  m_hdrRenderTarget = backend->createRenderTarget(colors, m_hdrDepth, extent.width, extent.height,
    vk::SampleCountFlagBits::e1);

  backend->updateDescriptorSetTexture(m_tonemapDescriptorSet, m_hdrColor, 0, kClampToEdgeSampler);
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
  backend->bindIndexBuffer(m_skyboxMesh->indexBuffer.getHandle(), m_skyboxMesh->indexBuffer.getIndexType());
  backend->bindVertexBuffer(m_skyboxMesh->vertexBuffer.getBuffer());
  const auto& face = m_skyboxMesh->faces.front();
  backend->drawIndexed(face.indexCount, 1, face.indexOffset);
}

void Renderer::endFrame() {
  m_renderAPI->endFrame();
}

void Renderer::prepare(Scene& scene, Query<Renderable>& renderables, const ViewInfo& camera) {
  auto& backend = *m_renderAPI;

  const SceneLighting* sceneLighting = camera.lighting;
  if (!sceneLighting) {
    static SceneLighting defaultSceneLighting {};
    sceneLighting = &defaultSceneLighting;
  }

  glm::vec3 lightDir = sceneLighting->lightDirection;

  m_perViewUniformBufferData.projection = camera.projection;
  m_perViewUniformBufferData.view = camera.view;
  m_perViewUniformBufferData.viewInverse = inverse(camera.view);
  m_perViewUniformBufferData.lightColorIntensity = glm::vec4(1.0f, 1.0f, 1.0f, sceneLighting->sunIntensity);
  m_perViewUniformBufferData.iblIntensity = sceneLighting->iblIntensity;
  m_perViewUniformBufferData.lightDirection = -lightDir;
  const IndirectLight* indirectLight = sceneLighting->indirectLight ? &*sceneLighting->indirectLight : nullptr;
  const Texture& iblSpecular = indirectLight ? *indirectLight->reflections : *m_defaultIblSpecular;
  m_perViewUniformBufferData.iblSpecularMaxLod = static_cast<float>(iblSpecular.getLevels() - 1);
  for (size_t i = 0; i < lighting::kIrradianceSHCount; i++) {
    m_perViewUniformBufferData.iblSH[i] = glm::vec4(indirectLight ? indirectLight->irradianceSH[i] : glm::vec3(0.0f), 0.0f);
  }

  prepareShadows(camera, lightDir);

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
    if (scene.registry().all_of<Disabled>(entity)) continue;
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
      if (!instance) {
        log::error("Renderable {}: face {} has no material instance, skipped", entt::to_integral(entity), i);
        continue;
      }
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
      entry.indexType = mesh->indexBuffer.getIndexType();
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

  for (uint32_t i = 0; i < m_shadowsUniformsBufferData.cascadeCount; i++) {
    backend.updateBuffer(m_shadowViewUniformBufferHandles[i], &m_shadowViewUniformBufferData[i], sizeof(PerViewUniforms));
  }
  backend.updateBuffer(m_viewUniformBufferHandle, &m_perViewUniformBufferData, sizeof(m_perViewUniformBufferData));
  const TonemapUniforms tonemapUniforms { .exposure = camera.exposure };
  backend.updateBuffer(m_tonemapUniformBuffer, &tonemapUniforms, sizeof(tonemapUniforms));
  backend.updateBuffer(m_shadowsUniformBufferHandle, &m_shadowsUniformsBufferData, sizeof(m_shadowsUniformsBufferData));
  backend.updateBuffer(m_lightsUniformBufferHandle, m_lightUniformsBufferData.data(), sizeof(m_lightUniformsBufferData));
  if (objectIndex > 0) {
    backend.updateBuffer(m_objectsUniformBufferHandle, m_perObjectUniformBufferData.data(), objectIndex * sizeof(PerObjectUniforms));
  }

  if (iblSpecular.getHandle() != m_iblSpecularMap) {
    m_iblSpecularMap = iblSpecular.getHandle();
    backend.updateDescriptorSetTexture(m_viewDescriptorSet, m_iblSpecularMap, std::to_underlying(PerViewDescriptorBindings::IBL_SPECULAR_MAP));
  }
}


void Renderer::prepareShadows(const ViewInfo& camera, const glm::vec3& lightDirection) {
  const ShadowSettings& settings = m_settings.shadows;
  m_shadowMapManager.update(camera, lightDirection, settings, m_settings.shadowMapSize);
  const auto cascades = m_shadowMapManager.cascades();

  auto& uniforms = m_shadowsUniformsBufferData;
  uniforms.cascadeCount = static_cast<uint32_t>(cascades.size());
  uniforms.cascadeBlend = std::clamp(settings.blendFraction, 0.0f, 1.0f);
  uniforms.flags = settings.visualizeCascades ? std::to_underlying(ShadowFlags::VisualizeCascades) : 0u;
  uniforms.cascadeSplits = glm::vec4(0.0f);

  constexpr float kTileScale = 1.0f / kShadowAtlasTiles;
  for (size_t i = 0; i < cascades.size(); i++) {
    const ShadowCascade& cascade = cascades[i];
    uniforms.cascadeSplits[static_cast<glm::length_t>(i)] = cascade.splitFar;
    uniforms.cascades[i].lightFromWorld = cascade.viewProjection;
    uniforms.cascades[i].atlasScaleOffset = glm::vec4(
      kTileScale, kTileScale,
      static_cast<float>(i % kShadowAtlasTiles) * kTileScale,
      static_cast<float>(i / kShadowAtlasTiles) * kTileScale);

    PerViewUniforms& view = m_shadowViewUniformBufferData[i];
    view.projection = cascade.projection;
    view.view = cascade.view;
    view.viewInverse = inverse(cascade.view);
    view.lightDirection = -lightDirection;
  }
}
}
