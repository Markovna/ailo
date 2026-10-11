#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "ModelDefinition.h"
#include "render/model/ModelPackage.h"

namespace modelc {

struct CompileError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct CompileOutput {
    ailo::model::ModelPackage package;
    std::vector<std::filesystem::path> dependencies;    // every file read besides the .model itself
    std::vector<std::string> warnings;
};

// definitionDir is the folder of the .model file: the source is read relative to it, and texture URIs are stored
// relative to it, which is where the runtime resolves them (next to the model's asset key).
CompileOutput compileModel(const ModelDefinition& def, const std::filesystem::path& definitionDir);

}
