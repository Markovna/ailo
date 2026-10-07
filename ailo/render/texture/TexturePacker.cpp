#include "TexturePacker.h"

#include <climits>
#include <cstring>
#include <fstream>
#include <iterator>
#include <utility>

#include "stb_image/stb_image.h"

namespace ailo::texture {

namespace fs = std::filesystem;

namespace {

constexpr int kChannels = STBI_rgb_alpha;

struct Image {
    uint32_t width = 0;
    uint32_t height = 0;
    TextureFormat format = TextureFormat::RGBA8_SRGB;
    std::vector<uint8_t> data;
};

std::string describe(const fs::path& path, const std::string& error) {
    return path.generic_string() + ": " + error;
}

bool readFile(const fs::path& path, std::vector<uint8_t>& out, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = describe(path, "cannot read file");
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
}

TextureFormat ldrFormat(const TextureImportOptions& options) {
    return options.srgb ? TextureFormat::RGBA8_SRGB : TextureFormat::RGBA8;
}

bool decode(std::span<const uint8_t> encoded, const TextureImportOptions& options, Image& out, std::string& error) {
    if (encoded.empty()) {
        error = "empty image";
        return false;
    }
    if (encoded.size() > size_t(INT_MAX)) {
        error = "image is larger than 2 GiB";
        return false;
    }

    auto* bytes = reinterpret_cast<const stbi_uc*>(encoded.data());
    const int length = int(encoded.size());
    const bool hdr = stbi_is_hdr_from_memory(bytes, length);

    int width = 0, height = 0, sourceChannels = 0;
    void* pixels = hdr
        ? static_cast<void*>(stbi_loadf_from_memory(bytes, length, &width, &height, &sourceChannels, kChannels))
        : static_cast<void*>(stbi_load_from_memory(bytes, length, &width, &height, &sourceChannels, kChannels));
    if (!pixels) {
        error = std::string("cannot decode image: ") + stbi_failure_reason();
        return false;
    }

    out.width = uint32_t(width);
    out.height = uint32_t(height);
    out.format = hdr ? TextureFormat::RGBA32F : ldrFormat(options);

    auto* first = static_cast<const uint8_t*>(pixels);
    out.data.assign(first, first + size_t(width) * height * bytesPerPixel(out.format));
    stbi_image_free(pixels);
    return true;
}

TexturePackage makePackage(TextureType type, const Image& first, const TextureImportOptions& options) {
    TexturePackage pkg;
    pkg.type = type;
    pkg.format = first.format;
    pkg.width = first.width;
    pkg.height = first.height;
    pkg.levels = 1;
    pkg.generateMipmaps = options.mipmaps && mipLevelCount(first.width, first.height) > 1;
    return pkg;
}

std::optional<TexturePackage> finish(TexturePackage pkg, std::string& error) {
    error = pkg.validate();
    if (!error.empty()) return std::nullopt;
    return pkg;
}

}

std::optional<TexturePackage> packTexture(const fs::path& source, const TextureImportOptions& options, std::string& error) {
    std::vector<uint8_t> encoded;
    if (!readFile(source, encoded, error)) return std::nullopt;

    auto pkg = packTextureFromMemory(encoded, options, error);
    if (!pkg) error = describe(source, error);
    return pkg;
}

std::optional<TexturePackage> packTextureFromMemory(std::span<const uint8_t> encoded, const TextureImportOptions& options,
                                                    std::string& error) {
    Image image;
    if (!decode(encoded, options, image, error)) return std::nullopt;

    TexturePackage pkg = makePackage(TextureType::Texture2D, image, options);
    pkg.data = std::move(image.data);
    return finish(std::move(pkg), error);
}

std::optional<TexturePackage> packTextureFromPixels(const void* pixels, uint32_t width, uint32_t height, PixelLayout layout,
                                                    const TextureImportOptions& options, std::string& error) {
    Image image { .width = width, .height = height, .format = ldrFormat(options) };
    const size_t size = size_t(width) * height * 4;
    if (!pixels || size == 0) {
        error = "texture has no pixels";
        return std::nullopt;
    }

    auto* bytes = static_cast<const uint8_t*>(pixels);
    image.data.assign(bytes, bytes + size);
    if (layout == PixelLayout::BGRA8) {
        for (size_t i = 0; i < size; i += 4) {
            std::swap(image.data[i], image.data[i + 2]);
        }
    }

    TexturePackage pkg = makePackage(TextureType::Texture2D, image, options);
    pkg.data = std::move(image.data);
    return finish(std::move(pkg), error);
}

std::optional<TexturePackage> packCubemap(std::span<const fs::path, 6> faces, const TextureImportOptions& options,
                                          std::string& error) {
    std::array<Image, 6> images;
    for (size_t i = 0; i < faces.size(); i++) {
        std::vector<uint8_t> encoded;
        if (!readFile(faces[i], encoded, error)) return std::nullopt;
        if (!decode(encoded, options, images[i], error)) {
            error = describe(faces[i], error);
            return std::nullopt;
        }

        const Image& face = images[i];
        if (face.width != face.height) {
            error = describe(faces[i], "cubemap face is " + std::to_string(face.width) + "x" +
                                       std::to_string(face.height) + ", faces must be square");
            return std::nullopt;
        }
        if (i > 0 && (face.width != images[0].width || face.format != images[0].format)) {
            error = describe(faces[i], "cubemap face is " + std::to_string(face.width) + "x" +
                                       std::to_string(face.height) + " " + std::string(toString(face.format)) + ", " +
                                       faces[0].generic_string() + " is " + std::to_string(images[0].width) + "x" +
                                       std::to_string(images[0].height) + " " +
                                       std::string(toString(images[0].format)));
            return std::nullopt;
        }
    }

    TexturePackage pkg = makePackage(TextureType::Cubemap, images[0], options);
    pkg.data.reserve(images[0].data.size() * images.size());
    for (const Image& face : images) {
        pkg.data.insert(pkg.data.end(), face.data.begin(), face.data.end());
    }
    return finish(std::move(pkg), error);
}

std::array<fs::path, 6> cubemapFacePaths(const fs::path& path) {
    static constexpr const char* kSuffixes[] = { "_px", "_nx", "_py", "_ny", "_pz", "_nz" };

    fs::path stem = path;
    stem.replace_extension();

    std::array<fs::path, 6> faces;
    for (size_t i = 0; i < faces.size(); i++) {
        faces[i] = stem;
        faces[i] += kSuffixes[i];
        faces[i] += path.extension();
    }
    return faces;
}

}
