#include "ShaderCompiler.h"

#include <fstream>
#include <sstream>

#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <SPIRV/GlslangToSpv.h>

namespace fs = std::filesystem;

namespace matcomp {

namespace {

// glslang needs process-wide initialization before the first TShader is created.
struct GlslangProcess {
    GlslangProcess() { glslang::InitializeProcess(); }
    ~GlslangProcess() { glslang::FinalizeProcess(); }
};

// glslang tries includeLocal first for #include "...", then falls back to includeSystem; #include <...> goes to
// includeSystem only. A result with an empty name is a failure whose data is the error message.
class Includer : public glslang::TShader::Includer {
public:
    Includer(const std::vector<fs::path>& includeDirs, std::set<fs::path>& dependencies)
        : m_includeDirs(includeDirs), m_dependencies(dependencies) {}

    IncludeResult* includeLocal(const char* requested, const char* requesting, size_t) override {
        fs::path parent = fs::path(requesting).parent_path();
        if (parent.empty()) return nullptr;
        return tryOpen(parent / requested);
    }

    IncludeResult* includeSystem(const char* requested, const char*, size_t) override {
        for (const auto& dir : m_includeDirs) {
            if (auto* result = tryOpen(dir / requested)) return result;
        }
        return makeResult("", std::string("cannot find include file \"") + requested + "\"");
    }

    void releaseInclude(IncludeResult* result) override {
        if (result) delete static_cast<Result*>(result->userData);
    }

private:
    struct Result {
        std::string content;
        IncludeResult include;

        Result(const std::string& name, std::string data)
            : content(std::move(data)), include(name, content.c_str(), content.size(), this) {}
    };

    IncludeResult* tryOpen(const fs::path& candidate) {
        std::ifstream file(candidate, std::ios::binary);
        if (!file) return nullptr;
        std::ostringstream content;
        content << file.rdbuf();

        fs::path resolved = fs::weakly_canonical(candidate);
        m_dependencies.insert(resolved);
        return makeResult(resolved.generic_string(), content.str());
    }

    static IncludeResult* makeResult(const std::string& name, std::string content) {
        return &(new Result(name, std::move(content)))->include;
    }

    const std::vector<fs::path>& m_includeDirs;
    std::set<fs::path>& m_dependencies;
};

}

ShaderCompiler::ShaderCompiler(std::vector<fs::path> includeDirs) : m_includeDirs(std::move(includeDirs)) {}

bool ShaderCompiler::compile(const std::string& source, ailo::material::ShaderStage stage, const std::string& sourceName,
                             std::vector<uint32_t>& spirv, std::string& error) {
    static GlslangProcess process;

    // Same settings as glslc's defaults, which the hand-written shaders are built with.
    EShLanguage language = stage == ailo::material::ShaderStage::Vertex ? EShLangVertex : EShLangFragment;
    glslang::TShader shader(language);

    const char* strings[] = { source.c_str() };
    const int lengths[] = { static_cast<int>(source.size()) };
    const char* names[] = { sourceName.c_str() };
    shader.setStringsWithLengthsAndNames(strings, lengths, names, 1);

    // glslc enables these implicitly: #include support, and #line with a file name.
    shader.setPreamble("#extension GL_GOOGLE_include_directive : require\n"
                       "#extension GL_GOOGLE_cpp_style_line_directive : require\n");
    shader.setEnvInput(glslang::EShSourceGlsl, language, glslang::EShClientVulkan, 100);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_0);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_0);

    auto messages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules);
    Includer includer(m_includeDirs, m_dependencies);
    if (!shader.parse(GetDefaultResources(), 110, false, messages, includer)) {
        error = shader.getInfoLog();
        return false;
    }

    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(messages)) {
        error = program.getInfoLog();
        return false;
    }

    spv::SpvBuildLogger logger;
    std::vector<unsigned int> words;
    glslang::GlslangToSpv(*program.getIntermediate(language), words, &logger);
    if (std::string messagesText = logger.getAllMessages(); !messagesText.empty() && words.empty()) {
        error = messagesText;
        return false;
    }

    spirv.assign(words.begin(), words.end());
    return true;
}

}
