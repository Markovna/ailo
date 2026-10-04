#pragma once

#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include "render/material/MaterialEnums.h"

namespace matcomp {

// #include "..." is resolved relative to the including file first, then against the include directories.
class ShaderCompiler {
public:
    explicit ShaderCompiler(std::vector<std::filesystem::path> includeDirs);

    bool compile(const std::string& source, ailo::material::ShaderStage stage, const std::string& sourceName,
                 std::vector<uint32_t>& spirv, std::string& error);

    const std::set<std::filesystem::path>& dependencies() const { return m_dependencies; }

private:
    std::vector<std::filesystem::path> m_includeDirs;
    std::set<std::filesystem::path> m_dependencies;
};

}
