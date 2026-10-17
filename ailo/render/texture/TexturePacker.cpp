#include "TexturePacker.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <numbers>
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
    out.data.assign(first, first + size_t(width) * height * blockBytes(out.format));
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
    pkg.generateMipmaps = options.mipmaps == MipmapMode::Runtime && mipLevelCount(first.width, first.height) > 1;
    return pkg;
}

// Bilinear sample at (u, v) in [0, 1], wrapping horizontally and clamping vertically.
void sampleEquirect(const Image& image, float u, float v, float out[4]) {
    const float fx = (u - std::floor(u)) * float(image.width) - 0.5f;
    const float fy = std::clamp(v, 0.0f, 1.0f) * float(image.height) - 0.5f;
    const float x0f = std::floor(fx), y0f = std::floor(fy);
    const float tx = fx - x0f, ty = fy - y0f;

    const int w = int(image.width), h = int(image.height);
    const int xs[2] = { ((int(x0f) % w) + w) % w, ((int(x0f) + 1) % w + w) % w };
    const int ys[2] = { std::clamp(int(y0f), 0, h - 1), std::clamp(int(y0f) + 1, 0, h - 1) };
    const float weights[2][2] = { { (1 - tx) * (1 - ty), tx * (1 - ty) }, { (1 - tx) * ty, tx * ty } };

    const bool hdr = image.format == TextureFormat::RGBA32F;
    std::fill(out, out + 4, 0.0f);
    for (int j = 0; j < 2; j++) {
        for (int i = 0; i < 2; i++) {
            const size_t index = (size_t(ys[j]) * image.width + xs[i]) * 4;
            for (int c = 0; c < 4; c++) {
                float value;
                if (hdr) std::memcpy(&value, image.data.data() + (index + c) * sizeof(float), sizeof(float));
                else value = image.data[index + c];
                out[c] += value * weights[j][i];
            }
        }
    }
}

float srgbToLinear(uint8_t value) {
    static const auto table = [] {
        std::array<float, 256> t;
        for (int i = 0; i < 256; i++) {
            const float c = float(i) / 255.0f;
            t[i] = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
        }
        return t;
    }();
    return table[value];
}

uint8_t linearToSrgb(float value) {
    const float c = std::clamp(value, 0.0f, 1.0f);
    const float s = c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
    return uint8_t(std::lround(s * 255.0f));
}

std::vector<float> toFloat(std::span<const uint8_t> image, TextureFormat format) {
    std::vector<float> out;
    if (format == TextureFormat::RGBA32F) {
        out.resize(image.size() / sizeof(float));
        std::memcpy(out.data(), image.data(), image.size());
        return out;
    }
    out.resize(image.size());
    const bool srgb = format == TextureFormat::RGBA8_SRGB;
    for (size_t i = 0; i < image.size(); i++) {
        out[i] = srgb && i % 4 != 3 ? srgbToLinear(image[i]) : float(image[i]) / 255.0f;
    }
    return out;
}

void fromFloat(std::span<const float> image, TextureFormat format, uint8_t* out) {
    if (format == TextureFormat::RGBA32F) {
        std::memcpy(out, image.data(), image.size_bytes());
        return;
    }
    const bool srgb = format == TextureFormat::RGBA8_SRGB;
    for (size_t i = 0; i < image.size(); i++) {
        out[i] = srgb && i % 4 != 3 ? linearToSrgb(image[i])
                                    : uint8_t(std::lround(std::clamp(image[i], 0.0f, 1.0f) * 255.0f));
    }
}

// Box-filters one axis of a width x height RGBA image down to `target` texels. Each source texel is weighted by how
// much of the destination texel it covers, so an odd size blends its last row or column in instead of dropping it.
std::vector<float> downsampleAxis(std::span<const float> src, uint32_t width, uint32_t height, uint32_t target,
                                  bool vertical) {
    const uint32_t length = vertical ? height : width;
    const uint32_t lines = vertical ? width : height;
    const uint32_t outWidth = vertical ? width : target;
    const size_t srcStep = vertical ? size_t(width) * 4 : 4;
    const size_t srcLine = vertical ? 4 : size_t(width) * 4;
    const size_t dstStep = vertical ? size_t(outWidth) * 4 : 4;
    const size_t dstLine = vertical ? 4 : size_t(outWidth) * 4;

    std::vector<float> dst(size_t(lines) * target * 4);
    const double scale = double(length) / double(target);
    for (uint32_t i = 0; i < target; i++) {
        const double lo = i * scale, hi = (i + 1) * scale;
        for (uint32_t j = uint32_t(lo); j < length && j < hi; j++) {
            const float weight = float((std::min(hi, j + 1.0) - std::max(lo, double(j))) / scale);
            for (uint32_t line = 0; line < lines; line++) {
                const float* s = src.data() + line * srcLine + j * srcStep;
                float* d = dst.data() + line * dstLine + i * dstStep;
                for (int c = 0; c < 4; c++) d[c] += weight * s[c];
            }
        }
    }
    return dst;
}

std::optional<TexturePackage> finish(TexturePackage pkg, const TextureImportOptions& options, std::string& error) {
    error = pkg.validate();
    if (!error.empty()) return std::nullopt;
    if (options.mipmaps == MipmapMode::Offline && !generateMipLevels(pkg, error)) return std::nullopt;
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
    return finish(std::move(pkg), options, error);
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
    return finish(std::move(pkg), options, error);
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
    return finish(std::move(pkg), options, error);
}

std::optional<TexturePackage> packCubemapFromEquirect(const fs::path& source, uint32_t faceSize,
                                                      const TextureImportOptions& options, std::string& error) {
    std::vector<uint8_t> encoded;
    if (!readFile(source, encoded, error)) return std::nullopt;

    Image image;
    if (!decode(encoded, options, image, error)) {
        error = describe(source, error);
        return std::nullopt;
    }

    const uint32_t size = faceSize ? faceSize : std::max(image.width / 4, 1u);
    TexturePackage pkg = makePackage(TextureType::Cubemap, Image { .width = size, .height = size, .format = image.format },
                                     options);
    pkg.data.resize(pkg.expectedDataSize());

    constexpr float pi = std::numbers::pi_v<float>;
    const bool hdr = image.format == TextureFormat::RGBA32F;
    const uint32_t pixelSize = blockBytes(image.format);

    // Unnormalized direction through (uc, vc) in [-1, 1] of a face, as Vulkan selects cube faces: +x -x +y -y +z -z.
    auto faceDirection = [](uint32_t face, float uc, float vc, float d[3]) {
        switch (face) {
            case 0:  d[0] = 1;   d[1] = -vc; d[2] = -uc; break;
            case 1:  d[0] = -1;  d[1] = -vc; d[2] = uc;  break;
            case 2:  d[0] = uc;  d[1] = 1;   d[2] = vc;  break;
            case 3:  d[0] = uc;  d[1] = -1;  d[2] = -vc; break;
            case 4:  d[0] = uc;  d[1] = -vc; d[2] = 1;   break;
            default: d[0] = -uc; d[1] = -vc; d[2] = -1;  break;
        }
        return std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    };

    const float sourceTexelHeight = pi / float(image.height);
    const float sourceTexelWidth = 2.0f * pi / float(image.width);
    const float minSinTheta = std::sin(0.5f * sourceTexelHeight);
    constexpr int kMaxGrid = 16;

    for (uint32_t face = 0; face < 6; face++) {
        uint8_t* faceData = pkg.data.data() + pkg.imageSize(0) * face;
        for (uint32_t y = 0; y < size; y++) {
            for (uint32_t x = 0; x < size; x++) {
                // Average n x n bilinear samples, n = how many source texels the face texel spans: one over most of
                // the sky, keeping the source's detail, more towards the poles, where source texels narrow by
                // sin(theta). Rounded, not ceiled: face centers are only ~1.27x minified, and averaging there blurs.
                float d[3];
                const float length = faceDirection(face, 2.0f * (float(x) + 0.5f) / float(size) - 1.0f,
                                                   2.0f * (float(y) + 0.5f) / float(size) - 1.0f, d);
                const float texelAngle = 2.0f / (float(size) * length);
                const float sinTheta = std::max(std::sqrt(std::max(1.0f - d[1] * d[1] / (length * length), 0.0f)),
                                                minSinTheta);
                const float sourceTexel = std::min(sourceTexelHeight, sourceTexelWidth * sinTheta);
                const int grid = std::clamp(int(std::lround(texelAngle / sourceTexel)), 1, kMaxGrid);

                float sum[4] = {};
                for (int sy = 0; sy < grid; sy++) {
                    for (int sx = 0; sx < grid; sx++) {
                        const float uc = 2.0f * (float(x) + (float(sx) + 0.5f) / float(grid)) / float(size) - 1.0f;
                        const float vc = 2.0f * (float(y) + (float(sy) + 0.5f) / float(grid)) / float(size) - 1.0f;
                        const float sampleLength = faceDirection(face, uc, vc, d);

                        // The mapping lightc prefilters with, so the sky matches its reflections.
                        const float u = (std::atan2(d[0], d[2]) + pi) / (2.0f * pi);
                        const float v = std::acos(std::clamp(d[1] / sampleLength, -1.0f, 1.0f)) / pi;

                        float sample[4];
                        sampleEquirect(image, u, v, sample);
                        for (int c = 0; c < 4; c++) sum[c] += sample[c];
                    }
                }

                uint8_t* pixel = faceData + (size_t(y) * size + x) * pixelSize;
                for (int c = 0; c < 4; c++) {
                    const float value = sum[c] / float(grid * grid);
                    if (hdr) {
                        std::memcpy(pixel + c * sizeof(float), &value, sizeof(float));
                    } else {
                        pixel[c] = uint8_t(std::clamp(std::lround(value), 0l, 255l));
                    }
                }
            }
        }
    }

    auto result = finish(std::move(pkg), options, error);
    if (!result) error = describe(source, error);
    return result;
}

bool generateMipLevels(TexturePackage& pkg, std::string& error) {
    if (pkg.levels != 1 || pkg.generateMipmaps) {
        error = "mip levels can only be generated for a package with one stored level";
        return false;
    }
    if (pkg.format == TextureFormat::RGBA16F || isCompressed(pkg.format)) {
        error = "cannot generate mip levels for " + std::string(toString(pkg.format));
        return false;
    }

    const uint32_t levels = mipLevelCount(pkg.width, pkg.height);
    if (levels == 1) return true;

    // Each level is filtered from the unquantized previous one, so 8-bit rounding doesn't accumulate down the chain.
    std::vector<std::vector<float>> layers;
    for (uint32_t layer = 0; layer < pkg.layers(); layer++) {
        layers.push_back(toFloat(pkg.image(0, layer), pkg.format));
    }

    pkg.levels = levels;
    pkg.data.resize(pkg.expectedDataSize());
    for (uint32_t level = 1; level < levels; level++) {
        const uint32_t width = pkg.levelWidth(level - 1), height = pkg.levelHeight(level - 1);
        const uint32_t targetWidth = pkg.levelWidth(level), targetHeight = pkg.levelHeight(level);
        for (uint32_t layer = 0; layer < pkg.layers(); layer++) {
            auto& image = layers[layer];
            image = downsampleAxis(downsampleAxis(image, width, height, targetWidth, false), targetWidth, height,
                                   targetHeight, true);
            const size_t offset = pkg.image(level, layer).data() - pkg.data.data();
            fromFloat(image, pkg.format, pkg.data.data() + offset);
        }
    }

    error = pkg.validate();
    return error.empty();
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
