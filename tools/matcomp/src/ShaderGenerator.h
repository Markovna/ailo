#pragma once

#include <string>
#include <vector>

#include "MaterialDefinition.h"

namespace matcomp {

std::vector<ailo::material::Variant> getStageVariants(const MaterialDefinition& def, ailo::material::ShaderStage stage);

std::string generateShader(const MaterialDefinition& def, ailo::material::ShaderStage stage,
                           ailo::material::Variant variant, const std::string& sourceName);

}
