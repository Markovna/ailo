#include "ShaderCompiler.h"

#include <fstream>
#include <memory>
#include <sstream>

#include <shaderc/shaderc.hpp>

namespace fs = std::filesystem;

namespace matcomp {

namespace {

class Includer : public shaderc::CompileOptions::IncluderInterface {
public:
    Includer(const std::vector<fs::path>& includeDirs, std::set<fs::path>& dependencies)
        : m_includeDirs(includeDirs), m_dependencies(dependencies) {}

    shaderc_include_result* GetInclude(const char* requested, shaderc_include_type type,
                                       const char* requesting, size_t) override {
        auto* result = new Result;

        std::vector<fs::path> candidates;
        if (type == shaderc_include_type_relative) {
            fs::path parent = fs::path(requesting).parent_path();
            if (!parent.empty()) candidates.push_back(parent / requested);
        }
        for (const auto& dir : m_includeDirs) candidates.push_back(dir / requested);

        for (const auto& candidate : candidates) {
            std::ifstream file(candidate, std::ios::binary);
            if (!file) continue;
            std::ostringstream content;
            content << file.rdbuf();

            fs::path resolved = fs::weakly_canonical(candidate);
            m_dependencies.insert(resolved);
            result->name = resolved.generic_string();
            result->content = content.str();
            return result->fill();
        }

        result->content = std::string("cannot find include file \"") + requested + "\"";
        return result->fill();   // empty name signals failure; content is the error message
    }

    void ReleaseInclude(shaderc_include_result* data) override {
        delete static_cast<Result*>(data->user_data);
    }

private:
    struct Result {
        std::string name;
        std::string content;
        shaderc_include_result include {};

        shaderc_include_result* fill() {
            include.source_name = name.c_str();
            include.source_name_length = name.size();
            include.content = content.c_str();
            include.content_length = content.size();
            include.user_data = this;
            return &include;
        }
    };

    const std::vector<fs::path>& m_includeDirs;
    std::set<fs::path>& m_dependencies;
};

}

ShaderCompiler::ShaderCompiler(std::vector<fs::path> includeDirs) : m_includeDirs(std::move(includeDirs)) {}

bool ShaderCompiler::compile(const std::string& source, ailo::material::ShaderStage stage, const std::string& sourceName,
                             std::vector<uint32_t>& spirv, std::string& error) {
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    // Same settings as glslc's defaults, which the hand-written shaders are built with.
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_0);
    options.SetSourceLanguage(shaderc_source_language_glsl);
    options.SetIncluder(std::make_unique<Includer>(m_includeDirs, m_dependencies));

    auto kind = stage == ailo::material::ShaderStage::Vertex ? shaderc_vertex_shader : shaderc_fragment_shader;
    auto result = compiler.CompileGlslToSpv(source, kind, sourceName.c_str(), options);

    if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
        error = result.GetErrorMessage();
        return false;
    }

    spirv.assign(result.cbegin(), result.cend());
    return true;
}

}
