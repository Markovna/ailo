#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ailo::fileio {

bool readFile(const std::filesystem::path& path, std::string& out);
bool readFile(const std::filesystem::path& path, std::vector<std::uint8_t>& out);

// creates missing parent directories
bool writeFile(const std::filesystem::path& path, const void* data, size_t size);

}
