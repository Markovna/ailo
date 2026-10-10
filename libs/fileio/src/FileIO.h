#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

namespace ailo::fileio {

bool readFile(const std::filesystem::path& path, std::string& out);

// creates missing parent directories
bool writeFile(const std::filesystem::path& path, const void* data, size_t size);

}
