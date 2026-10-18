#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace ailo::os {

std::vector<char> readFile(const std::string& filename);

// The folder of the running executable. Throws std::runtime_error if it cannot be determined.
std::filesystem::path executableDirectory();

}
