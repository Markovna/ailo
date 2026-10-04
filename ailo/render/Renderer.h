#pragma once

#include "RenderPrimitive.h"
#include <functional>
#include <string>
#include <vector>

#include "Renderable.h"

namespace ailo {

struct PerViewUniforms {
  alignas(16) glm::mat4 projection;
  alignas(16) glm::mat4 view;
  alignas(16) glm::mat4 viewInverse;

  glm::vec3 lightDirection;
  float __padding0;
  glm::vec4 lightColorIntensity;
  glm::vec4 ambientLightColorIntensity;

  float iblSpecularMaxLod;
  float __padding1[3];
  alignas(16) glm::mat4 lightViewProjection;
};

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

struct Camera {
  glm::mat4 projection = glm::mat4(1.0f);
  glm::mat4 view = glm::mat4(1.0f);
};

struct RendererSettings {
  uint32_t shadowMapSize = 1024;
  glm::vec4 clearColor { 0.1f, 0.1f, 0.3f, 1.0f };
  std::string dfgLutPath = "assets/textures/dfg_lut.hdr";
};

enum class DescriptorSetBindingPoints : uint8_t {
  PER_VIEW        = 0,
  PER_RENDERABLE  = 1,
  PER_MATERIAL    = 2
};

enum class PerViewDescriptorBindings {
  FRAME_UNIFORMS = 0,
  LIGHTS = 1,
  IBL_SPECULAR_MAP = 2,
  IBL_DFG_LUT = 3,
  SHADOW_MAP = 4
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
class Shader;

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
  BufferHandle vertexBuffer;
  uint32_t indexCount;
  uint32_t indexOffset;
};

class Renderer {
public:
  using OverlayPass = std::move_only_function<void()>;

  Renderer(RenderAPI*, AssetManager*, const RendererSettings& settings = {});
  ~Renderer();

  // Records a full frame: beginFrame, shadow pass, color pass, overlay passes, endFrame.
  // Skips the frame if the swapchain image could not be acquired (e.g. during resize).
  void render(Scene& scene, Query<Renderable> renderables, const Camera& camera);

  // Passes recorded after the scene passes, in registration order (e.g. UI). Each pass begins its own render pass.
  void addOverlayPass(OverlayPass pass);

  bool beginFrame();
  void prepare(Scene& scene, Query<Renderable>& renderables, const Camera& camera);
  void shadowPass();
  void colorPass();
  void endFrame();

  // Drops the renderer's asset references so the AssetManager can free them before it shuts down.
  // GPU objects owned by the renderer are released by its destructor.
  void releaseAssets();
  TextureHandle getShadowMapTexture() const { return m_shadowMapTexture; }

private:
  using PerObjectUniformBufferData = std::vector<PerObjectUniforms>;

  asset_ptr<Texture> createWhiteTexture(AssetManager*);
  asset_ptr<Texture> createBlackTexture(AssetManager*);
  asset_ptr<Texture> createDefaultNormalTexture(AssetManager*);
  asset_ptr<Texture> createDefaultMetallicRoughnessTexture(AssetManager*);
  asset_ptr<Texture> createBlackCubemapTexture(AssetManager*);

  void drawSkybox();

  PerObjectUniformBufferData m_perObjectUniformBufferData {32};
  PerViewUniforms m_perViewUniformBufferData {};
  PerViewUniforms m_shadowViewUniformBufferData {};
  std::array<LightUniform, kLightUniformArraySize> m_lightUniformsBufferData {};

  Unique<gpu::Buffer> m_objectsUniformBufferHandle;
  Unique<gpu::Buffer> m_viewUniformBufferHandle;
  Unique<gpu::Buffer> m_shadowViewUniformBufferHandle;
  Unique<gpu::Buffer> m_lightsUniformBufferHandle;
  Unique<gpu::DescriptorSetLayout> m_viewDescriptorSetLayout;
  Unique<gpu::DescriptorSetLayout> m_objectDescriptorSetLayout;
  Unique<gpu::DescriptorSet> m_viewDescriptorSet;
  Unique<gpu::DescriptorSet> m_shadowViewDescriptorSet;
  Unique<gpu::DescriptorSet> m_objectDescriptorSet;
  bool m_objectDescriptorSetDirty = true;
  asset_ptr<Texture> m_iblDfgLut;
  TextureHandle m_iblSpecularMap;

  std::vector<asset_ptr<Asset>> m_persistentAssets;

  // Shadow mapping
  Unique<gpu::Texture> m_shadowMapTexture;
  Unique<gpu::RenderTarget> m_shadowMapRenderTarget;

  // Skybox (still a hand-written shader rather than a material)
  asset_ptr<Shader> m_skyboxShader;
  asset_ptr<Mesh> m_skyboxMesh;
  Unique<gpu::DescriptorSet> m_skyboxDescriptorSet;
  TextureHandle m_skyboxTexture;

  RendererSettings m_settings;
  std::vector<OverlayPass> m_overlayPasses;

  Unique<gpu::Buffer> m_dummyBonesBuffer;

  std::vector<RenderData> m_renderData;
  RenderAPI* m_renderAPI;
};

}
