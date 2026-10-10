#include "FileIO.h"

#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace ailo::fileio {

bool readFile(const fs::path& path, std::string& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::ostringstream content;
    content << file.rdbuf();
    out = content.str();
    return true;
}

bool writeFile(const fs::path& path, const void* data, size_t size) {
    if (path.has_parent_path()) fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file.write(static_cast<const char*>(data), std::streamsize(size));
    return bool(file);
}

}
