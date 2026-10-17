#pragma once

#include "RenderPrimitive.h"
#include <functional>
#include <string>
#include <vector>

#include "Renderable.h"
#include "ShadowMapManager.h"
#include "render/lighting/LightingPackage.h"

namespace ailo {

struct PerViewUniforms {
  alignas(16) glm::mat4 projection;
  alignas(16) glm::mat4 view;
  alignas(16) glm::mat4 viewInverse;

  glm::vec3 lightDirection;
  float __padding0;
  glm::vec4 lightColorIntensity;

  float iblSpecularMaxLod;
  uint32_t lightCount;
  float iblIntensity;
  float __padding1;
  glm::vec4 iblSH[lighting::kIrradianceSHCount];
};

struct ShadowCascadeUniform {
  alignas(16) glm::mat4 lightFromWorld;
  // Cascade tile in the atlas: uv * xy + zw.
  glm::vec4 atlasScaleOffset;
};

enum class ShadowFlags : uint32_t {
  None = 0,
  VisualizeCascades = 1 << 0
};

struct ShadowUniforms {
  ShadowCascadeUniform cascades[kMaxShadowCascades];
  // View-space distance where each cascade ends.
  glm::vec4 cascadeSplits;
  uint32_t cascadeCount;
  float cascadeBlend;
  uint32_t flags;
  float __padding0;
};
static_assert(kMaxShadowCascades <= 4, "cascadeSplits holds one split per vec4 component");

struct LightUniform {
  glm::vec4 lightPositionFalloff;
  glm::vec4 lightColorIntensity;
  glm::vec3 direction;
  uint32_t type; // 0-point, 1-spot
  glm::vec2 scaleOffset; // spot light only
  float __padding0;
  float __padding1;
};

struct alignas(64) PerObjectUniforms {
  alignas(16) glm::mat4 model = glm::mat4(1);
  alignas(16) glm::mat4 modelInverse = glm::mat4(1);
  alignas(16) glm::mat4 modelInverseTranspose = glm::mat4(1);
  uint32_t flags;
};

enum class ObjectFlags : uint32_t {
  None = 0,
  SkinningEnabled = 1 << 0
};

struct BonesUniform {
  constexpr static uint32_t kMaxBones = 256;
  struct Bone {
    glm::mat4 transform;
  };

  Bone bones[kMaxBones];
};

struct Skybox;
struct SceneLighting;

struct ViewInfo {
  glm::mat4 projection = glm::mat4(1.0f);
  glm::mat4 view = glm::mat4(1.0f);
  float nearPlane = 0.1f;
  float farPlane = 1000.0f;
  float exposure = 1.0f;
  const Skybox* skybox = nullptr;
  const SceneLighting* lighting = nullptr;
};

struct RendererSettings {
  // Resolution of one cascade; the atlas is kShadowAtlasTiles times larger on each side.
  uint32_t shadowMapSize = 1024;
  ShadowSettings shadows;
  glm::vec4 clearColor { 0.1f, 0.1f, 0.3f, 1.0f };
};

enum class DescriptorSetBindingPoints : uint8_t {
  PER_VIEW        = 0,
  PER_RENDERABLE  = 1,
  PER_MATERIAL    = 2
};

enum class PerViewDescriptorBindings {
  FRAME_UNIFORMS = 0,
  SHADOWS = 1,
  LIGHTS = 2,
  IBL_SPECULAR_MAP = 3,
  IBL_DFG_LUT = 4,
  SHADOW_MAP = 5
};

enum class PerObjectDescriptorBindings {
  OBJECT_UNIFORMS = 0,
  BONE_UNIFORMS = 1
};

class DescriptorSetLayoutBindings {
public:
  static const std::vector<DescriptorSetLayoutBinding>& perView() {
    static std::vector<DescriptorSetLayoutBinding> bindings {
      {
        .binding = std::to_underlying(PerViewDescriptorBindings::FRAME_UNIFORMS),
        .descriptorType = vk::DescriptorType::eUniformBuffer,
        .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
      },
    {
        .binding = std::to_underlying(PerViewDescriptorBindings::SHADOWS),
        .descriptorType = vk::DescriptorType::eUniformBuffer,
        .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
      },
    {
        .binding = std::to_underlying(PerViewDescriptorBindings::LIGHTS),
        .descriptorType = vk::DescriptorType::eUniformBuffer,
        .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
      },
      {
        .binding = std::to_underlying(PerViewDescriptorBindings::IBL_SPECULAR_MAP),
        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
        .stageFlags = vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eVertex
      },
      {
        .binding = std::to_underlying(PerViewDescriptorBindings::IBL_DFG_LUT),
        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
        .stageFlags = vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eVertex
      },
      {
        .binding = std::to_underlying(PerViewDescriptorBindings::SHADOW_MAP),
        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
        .stageFlags = vk::ShaderStageFlagBits::eFragment
      }
    };
    return bindings;
  }

    static const std::vector<DescriptorSetLayoutBinding>& perObject() {
        static std::vector<DescriptorSetLayoutBinding> bindings {
            {
              .binding = std::to_underlying(PerObjectDescriptorBindings::OBJECT_UNIFORMS),
              .descriptorType = vk::DescriptorType::eUniformBufferDynamic,
              .stageFlags = vk::ShaderStageFlagBits::eVertex
            },
            {
              .binding = std::to_underlying(PerObjectDescriptorBindings::BONE_UNIFORMS),
              .descriptorType = vk::DescriptorType::eUniformBufferDynamic,
              .stageFlags = vk::ShaderStageFlagBits::eVertex
            }
        };
        return bindings;
    }
};

class Scene;

template<typename... Cs>
class Query;

struct RenderData {
  ProgramHandle colorProgram;
  ProgramHandle depthProgram;
  VertexBufferLayoutHandle vertexBufferLayout;
  DescriptorSetHandle objectDescriptorSet;
  uint32_t objectBufferOffset;
  const MaterialInstance* materialInstance;
  BufferHandle indexBuffer;
  IndexType indexType;
  BufferHandle vertexBuffer;
  uint32_t indexCount;
  uint32_t indexOffset;
};

class Renderer {
public:
  using OverlayPass = std::move_only_function<void()>;

  Renderer(RenderAPI*, AssetServer&, AssetStorage<MaterialInstance>&,
           const RendererSettings& settings = {});
  ~Renderer();

  void render(Scene& scene, Query<Renderable> renderables, const ViewInfo& camera);

  // Passes recorded after the scene passes, in registration order (e.g. UI). Each pass begins its own render pass.
  void addOverlayPass(OverlayPass pass);

  bool beginFrame();
  void prepare(Scene& scene, Query<Renderable>& renderables, const ViewInfo& camera);
  void prepareShadows(const ViewInfo& camera, const glm::vec3& lightDirection);
  void shadowPass();
  void colorPass();
  void tonemapPass();
  void endFrame();

  TextureHandle getShadowMapTexture() const { return m_shadowMapTexture; }
  // Read every frame, so it can be tweaked at runtime.
  ShadowSettings& shadowSettings() { return m_settings.shadows; }

private:
  using PerObjectUniformBufferData = std::vector<PerObjectUniforms>;

  void drawSkybox();
  void updateHdrTarget();

  PerObjectUniformBufferData m_perObjectUniformBufferData {32};
  PerViewUniforms m_perViewUniformBufferData {};
  std::array<PerViewUniforms, kMaxShadowCascades> m_shadowViewUniformBufferData {};
  std::array<LightUniform, kLightUniformArraySize> m_lightUniformsBufferData {};
  ShadowUniforms m_shadowsUniformsBufferData {};

  Unique<gpu::Buffer> m_objectsUniformBufferHandle;
  Unique<gpu::Buffer> m_viewUniformBufferHandle;
  std::array<Unique<gpu::Buffer>, kMaxShadowCascades> m_shadowViewUniformBufferHandles;
  Unique<gpu::Buffer> m_lightsUniformBufferHandle;
  Unique<gpu::Buffer> m_shadowsUniformBufferHandle;
  Unique<gpu::DescriptorSetLayout> m_viewDescriptorSetLayout;
  Unique<gpu::DescriptorSetLayout> m_objectDescriptorSetLayout;
  Unique<gpu::DescriptorSet> m_viewDescriptorSet;
  std::array<Unique<gpu::DescriptorSet>, kMaxShadowCascades> m_shadowViewDescriptorSets;
  Unique<gpu::DescriptorSet> m_objectDescriptorSet;
  bool m_objectDescriptorSetDirty = true;
  AssetPtr<Texture> m_iblDfgLut;
  AssetPtr<Texture> m_defaultIblSpecular;
  TextureHandle m_iblSpecularMap;

  // Shadow mapping
  Unique<gpu::Texture> m_shadowMapTexture;
  Unique<gpu::RenderTarget> m_shadowMapRenderTarget;

  // HDR color pass target, resolved into the swapchain by the tonemap pass. Recreated when the swapchain resizes.
  Unique<gpu::Texture> m_hdrColor;
  Unique<gpu::Texture> m_hdrDepth;
  Unique<gpu::RenderTarget> m_hdrRenderTarget;
  vk::Extent2D m_hdrExtent;
  Unique<gpu::DescriptorSetLayout> m_tonemapDescriptorSetLayout;
  Unique<gpu::DescriptorSet> m_tonemapDescriptorSet;
  Unique<gpu::Buffer> m_tonemapUniformBuffer;
  Unique<gpu::Program> m_tonemapProgram;

  AssetPtr<MaterialInstance> m_skyboxMaterial;
  AssetPtr<Mesh> m_skyboxMesh;
  TextureHandle m_skyboxTexture;
  bool m_drawSkybox = false;
  uint32_t m_skyboxObjectBufferOffset = 0;

  RendererSettings m_settings;
  std::vector<OverlayPass> m_overlayPasses;

  Unique<gpu::Buffer> m_dummyBonesBuffer;

  std::vector<RenderData> m_renderData;
  ShadowMapManager m_shadowMapManager;
  RenderAPI* m_renderAPI;
};

}
