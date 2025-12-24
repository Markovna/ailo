#pragma once

#include "RenderPrimitive.h"
#include <vector>

namespace ailo {

struct PerViewUniforms {
  alignas(16) glm::mat4 projection;
  alignas(16) glm::mat4 view;
  alignas(16) glm::mat4 viewInverse;

  glm::vec3 lightDirection;
  float __padding0;
  glm::vec4 lightColorIntensity;
  glm::vec4 ambientLightColorIntensity;
};

struct LightUniform {
  glm::vec4 lightPositionRadius;
  glm::vec4 lightColorIntensity;
  glm::vec3 direction;
  uint32_t type; // 0-point, 1-spot
  glm::vec2 scaleOffset; // spot light only
  float __padding0;
  float __padding1;
};

struct PerObjectUniforms {
  alignas(16) glm::mat4 model = glm::mat4(1);
  alignas(16) glm::mat4 modelInverse = glm::mat4(1);
  alignas(16) glm::mat4 modelInverseTranspose = glm::mat4(1);
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
  LIGHTS = 1
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
      }
    };
    return bindings;
  }

  static const std::vector<DescriptorSetLayoutBinding>& perObject() {
    static std::vector<DescriptorSetLayoutBinding> bindings {
        {
          .binding = 0,
          .descriptorType = vk::DescriptorType::eUniformBufferDynamic,
          .stageFlags = vk::ShaderStageFlagBits::eVertex
        }
    };
    return bindings;
  }
};

class Scene;

class Renderer {
 public:
  void render(Engine&, Scene& scene, const Camera&);
  void terminate(Engine&);

 private:
  void prepare(RenderAPI& backend, Scene&);

  using PerObjectUniformBufferData = std::vector<PerObjectUniforms>;

  PerObjectUniformBufferData m_perObjectUniformBufferData;
  PerViewUniforms m_perViewUniformBufferData {};
  LightUniform m_lightUniformsBufferData {};

  BufferHandle m_objectsUniformBufferHandle;
  BufferHandle m_viewUniformBufferHandle;
  BufferHandle m_lightsUniformBufferHandle;
  DescriptorSetHandle m_viewDescriptorSet;
  DescriptorSetHandle m_objectDescriptorSet;
  DescriptorSetLayoutHandle m_viewDescriptorSetLayout;
  DescriptorSetLayoutHandle m_objectDescriptorSetLayout;
};

}