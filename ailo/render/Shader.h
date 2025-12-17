#pragma once

#include "RenderAPI.h"

namespace ailo {

class Engine;

class Shader {
 public:
  Shader(Engine&, const PipelineDescription& description);
  PipelineHandle getPipeline() const { return m_pipeline; }

  DescriptorSetLayoutHandle getDescriptorSetLayout(uint32_t setIndex) const {
      if (setIndex >= m_descriptorSetLayouts.size()) {
          return {};
      }

    return m_descriptorSetLayouts[setIndex];
  }

  void destroy(Engine&);

 private:
  std::vector<DescriptorSetLayoutHandle> m_descriptorSetLayouts;
  PipelineHandle m_pipeline;
};

}