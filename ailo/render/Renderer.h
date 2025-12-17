#pragma once

#include "RenderPrimitive.h"
#include <vector>

namespace ailo {

struct PerViewUniforms {
  alignas(16) glm::mat4 projection;
  alignas(16) glm::mat4 view;
};

struct PerObjectUniforms {
  alignas(16) glm::mat4 model = glm::mat4(1);
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

class DescriptorSetLayoutBindings {
public:
  static const std::vector<DescriptorSetLayoutBinding>& perView() {
    static std::vector<DescriptorSetLayoutBinding> bindings {
      {
        .binding = 0,
        .descriptorType = vk::DescriptorType::eUniformBuffer,
        .stageFlags = vk::ShaderStageFlagBits::eVertex
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

  PerObjectUniformBufferData perObjectUniformBufferData;
  PerViewUniforms perViewUniformBufferData;

  BufferHandle m_objectsUniformBufferHandle;
  BufferHandle m_viewUniformBufferHandle;
  DescriptorSetHandle m_viewDescriptorSet;
  DescriptorSetHandle m_objectDescriptorSet;
  DescriptorSetLayoutHandle m_viewDescriptorSetLayout;
  DescriptorSetLayoutHandle m_objectDescriptorSetLayout;
};

}