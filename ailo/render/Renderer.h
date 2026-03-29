#pragma once

#include "RenderPrimitive.h"
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
  glm::mat4 projection;
  glm::mat4 view;
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

struct RenderData {
  ProgramHandle program;
  VertexBufferLayoutHandle vertexBufferLayout;
  DescriptorSetHandle objectDescriptorSet;
  uint32_t objectBufferOffset;
  Material* material;
  BufferHandle indexBuffer;
  BufferHandle vertexBuffer;
  uint32_t indexCount;
  uint32_t indexOffset;
  bool hasTransform;
  bool isSkinned;
};

class Renderer {
public:
  Renderer(Engine&);
  ~Renderer();

  bool beginFrame(Engine&);
  void shadowPass(Engine&, Scene& scene);
  void colorPass(Engine&, Scene& scene, const Camera& camera);
  void endFrame(Engine&);
  void onSceneCreated(Engine&, Scene&);

  void terminate(Engine&);
  TextureHandle getShadowMapTexture() const { return m_shadowMapTexture; }

private:
  void prepare(Engine&, Scene&);
  void onDestroyRenderable(entt::registry& registry, entt::entity entity);

  using PerObjectUniformBufferData = std::vector<PerObjectUniforms>;

  static asset_ptr<Texture> createWhiteTexture(Engine&);
  static asset_ptr<Texture> createBlackTexture(Engine&);
  static asset_ptr<Texture> createDefaultNormalTexture(Engine&);
  static asset_ptr<Texture> createDefaultMetallicRoughnessTexture(Engine&);

  PerObjectUniformBufferData m_perObjectUniformBufferData {32};
  PerViewUniforms m_perViewUniformBufferData {};
  std::array<LightUniform, kLightUniformArraySize> m_lightUniformsBufferData {};

  BufferHandle m_objectsUniformBufferHandle;
  BufferHandle m_viewUniformBufferHandle;
  BufferHandle m_lightsUniformBufferHandle;
  DescriptorSetHandle m_viewDescriptorSet;
  DescriptorSetHandle m_objectDescriptorSet;
  DescriptorSetLayoutHandle m_viewDescriptorSetLayout;
  DescriptorSetLayoutHandle m_objectDescriptorSetLayout;
  asset_ptr<Texture> m_iblDfgLut;
  TextureHandle m_iblSpecularMap;

  std::vector<asset_ptr<Texture>> m_persistentTextures;

  // Shadow mapping
  TextureHandle m_shadowMapTexture;
  RenderTargetHandle m_shadowMapRenderTarget;
  asset_ptr<Shader> m_shadowShader;
  asset_ptr<Shader> m_skinnedShadowShader;
  static constexpr uint32_t kShadowMapSize = 1024;

  BufferHandle m_dummyBonesBuffer;

  std::vector<RenderData> m_renderData;
  RenderAPI* m_renderAPI;
};

}
