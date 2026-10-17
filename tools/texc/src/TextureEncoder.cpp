#include "TextureEncoder.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#include "encoder/basisu_bc7e_scalar.h"
#include "encoder/basisu_enc.h"
#include "transcoder/basisu_astc_hdr_core.h"
#include "transcoder/basisu_transcoder_internal.h"
#include "transcoder/basisu_transcoder_uastc.h"

using namespace ailo::texture;

namespace texc {

namespace {

bool isHdrFormat(TextureFormat format) {
    return format == TextureFormat::BC6H || format == TextureFormat::RGBA16F || format == TextureFormat::RGBA32F;
}

void initEncoders() {
    static std::once_flag once;
    std::call_once(once, [] {
        basisu::basisu_encoder_init();
        bc7e_scalar::bc7e_compress_block_init();
    });
}

// Runs fn(i) for every i in [0, count) on all cores.
template<typename F>
void parallelFor(uint32_t count, F&& fn) {
    const uint32_t threads = std::clamp(std::thread::hardware_concurrency(), 1u, std::max(count, 1u));
    std::atomic<uint32_t> next = 0;
    auto worker = [&] {
        for (uint32_t i = next++; i < count; i = next++) fn(i);
    };
    std::vector<std::jthread> pool;
    for (uint32_t t = 1; t < threads; t++) pool.emplace_back(worker);
    worker();
}

struct Encoder {
    TextureFormat format;
    Quality quality;
    uint32_t bc1Flags = 0;
    bc7e_scalar::bc7e_compress_block_params bc7e {};
    basist::astc_6x6_hdr::fast_bc6h_params bc6h;

    Encoder(TextureFormat format, Quality quality) : format(format), quality(quality) {
        switch (quality) {
            case Quality::Low:    bc1Flags = 0; break;
            case Quality::Normal: bc1Flags = basist::cEncodeBC1HighQuality; break;
            case Quality::High:   bc1Flags = basist::cEncodeBC1HigherQuality; break;
        }

        // bc7e weighs errors perceptually for sRGB data.
        const bool perceptual = isSrgb(format);
        if (quality == Quality::Normal) bc7e_scalar::bc7e_compress_block_params_init_fast(&bc7e, perceptual);
        if (quality == Quality::High) bc7e_scalar::bc7e_compress_block_params_init_slow(&bc7e, perceptual);

        switch (quality) {
            case Quality::Low:
                bc6h.m_max_2subset_pats_to_try = 0;
                break;
            case Quality::Normal:
                bc6h.m_num_diff_endpoint_modes_to_try = basist::astc_6x6_hdr::BC6H_NUM_DIFF_ENDPOINT_MODES_TO_TRY_4;
                bc6h.m_max_2subset_pats_to_try = 1;
                break;
            case Quality::High:
                bc6h.m_num_diff_endpoint_modes_to_try = basist::astc_6x6_hdr::BC6H_NUM_DIFF_ENDPOINT_MODES_TO_TRY_9;
                bc6h.m_max_2subset_pats_to_try = 32;
                bc6h.m_brute_force_weight4_assignment = true;
                break;
        }
    }

    // One row of blocks of an rgba8 image, clamping the blocks that cross its right or bottom edge.
    void encodeLdrRow(const uint8_t* image, uint32_t width, uint32_t height, uint32_t by, uint8_t* out) const {
        const uint32_t blocksX = (width + 3) / 4;
        std::vector<basist::color_rgba> pixels(size_t(blocksX) * 16);
        for (uint32_t bx = 0; bx < blocksX; bx++) {
            for (uint32_t i = 0; i < 16; i++) {
                const uint32_t x = std::min(bx * 4 + i % 4, width - 1), y = std::min(by * 4 + i / 4, height - 1);
                std::memcpy(&pixels[bx * 16 + i], image + (size_t(y) * width + x) * 4, 4);
            }
        }

        if ((format == TextureFormat::BC7 || format == TextureFormat::BC7_SRGB) && quality != Quality::Low) {
            std::vector<uint64_t> blocks(size_t(blocksX) * 2);
            bc7e_scalar::bc7e_compress_blocks(blocksX, blocks.data(), reinterpret_cast<const uint32_t*>(pixels.data()),
                                             &bc7e);
            std::memcpy(out, blocks.data(), blocks.size() * sizeof(uint64_t));
            return;
        }

        const uint32_t size = blockBytes(format);
        for (uint32_t bx = 0; bx < blocksX; bx++) {
            const basist::color_rgba* block = &pixels[bx * 16];
            uint8_t* dst = out + size_t(bx) * size;
            switch (format) {
                case TextureFormat::BC1:
                case TextureFormat::BC1_SRGB:
                    basist::encode_bc1(dst, &block->r, bc1Flags);
                    break;
                case TextureFormat::BC3:
                case TextureFormat::BC3_SRGB:
                    basist::encode_bc4(dst, &block->a, sizeof(basist::color_rgba));
                    basist::encode_bc1(dst + 8, &block->r, bc1Flags);
                    break;
                case TextureFormat::BC4:
                    basist::encode_bc4(dst, &block->r, sizeof(basist::color_rgba));
                    break;
                case TextureFormat::BC5:
                    basist::encode_bc4(dst, &block->r, sizeof(basist::color_rgba));
                    basist::encode_bc4(dst + 8, &block->g, sizeof(basist::color_rgba));
                    break;
                case TextureFormat::BC7:
                case TextureFormat::BC7_SRGB:
                    basist::bc7f::fast_pack_bc7_auto_rgba(dst, block,
                                                          basist::bc7f::cPackBC7FlagDefaultPartiallyAnalytical);
                    break;
                default:
                    break;
            }
        }
    }

    // One row of blocks of an rgba32f image into BC6H, which stores unsigned half floats and no alpha.
    void encodeHdrRow(const float* image, uint32_t width, uint32_t height, uint32_t by, uint8_t* out) const {
        const uint32_t blocksX = (width + 3) / 4;
        for (uint32_t bx = 0; bx < blocksX; bx++) {
            basist::half_float pixels[16 * 3];
            for (uint32_t i = 0; i < 16; i++) {
                const uint32_t x = std::min(bx * 4 + i % 4, width - 1), y = std::min(by * 4 + i / 4, height - 1);
                for (uint32_t c = 0; c < 3; c++) {
                    float value = image[(size_t(y) * width + x) * 4 + c];
                    value = std::isnan(value) ? 0.0f : std::clamp(value, 0.0f, float(basist::MAX_HALF_FLOAT));
                    pixels[i * 3 + c] = basist::float_to_half(value);
                }
            }
            basist::astc_6x6_hdr::fast_encode_bc6h(pixels, reinterpret_cast<basist::bc6h_block*>(out + size_t(bx) * 16),
                                                   bc6h);
        }
    }
};

}

bool hasAlpha(const TexturePackage& pkg) {
    if (pkg.format != TextureFormat::RGBA8 && pkg.format != TextureFormat::RGBA8_SRGB) return false;
    for (size_t i = 3; i < pkg.data.size(); i += 4) {
        if (pkg.data[i] != 255) return true;
    }
    return false;
}

std::optional<TexturePackage> encodeTexture(const TexturePackage& source, TextureFormat format, Quality quality,
                                            std::string& error) {
    const bool hdrSource = source.format == TextureFormat::RGBA32F;
    if (source.format != TextureFormat::RGBA8 && source.format != TextureFormat::RGBA8_SRGB && !hdrSource) {
        error = "cannot encode a " + std::string(toString(source.format)) + " package";
        return std::nullopt;
    }
    if (hdrSource != isHdrFormat(format)) {
        error = std::string(hdrSource ? "an HDR" : "an LDR") + " image cannot be stored as " + std::string(toString(format));
        return std::nullopt;
    }
    if (isCompressed(format) && source.generateMipmaps) {
        error = std::string(toString(format)) + " needs its mip levels generated offline";
        return std::nullopt;
    }

    TexturePackage pkg = source;
    pkg.format = format;
    pkg.data.assign(pkg.expectedDataSize(), 0);

    for (uint32_t level = 0; level < pkg.levels; level++) {
        const uint32_t width = pkg.levelWidth(level), height = pkg.levelHeight(level);
        for (uint32_t layer = 0; layer < pkg.layers(); layer++) {
            auto src = source.image(level, layer);
            uint8_t* dst = pkg.data.data() + (pkg.image(level, layer).data() - pkg.data.data());

            if (format == TextureFormat::RGBA16F) {
                for (size_t i = 0; i < src.size() / sizeof(float); i++) {
                    float value;
                    std::memcpy(&value, src.data() + i * sizeof(float), sizeof(float));
                    const basist::half_float half = basist::float_to_half(value);
                    std::memcpy(dst + i * sizeof(half), &half, sizeof(half));
                }
            } else if (!isCompressed(format)) {
                std::memcpy(dst, src.data(), src.size());
            } else {
                initEncoders();
                const Encoder encoder(format, quality);
                const uint32_t blocksX = (width + 3) / 4, blocksY = (height + 3) / 4;
                const size_t rowBytes = size_t(blocksX) * blockBytes(format);
                parallelFor(blocksY, [&](uint32_t by) {
                    if (hdrSource) {
                        encoder.encodeHdrRow(reinterpret_cast<const float*>(src.data()), width, height, by,
                                             dst + by * rowBytes);
                    } else {
                        encoder.encodeLdrRow(src.data(), width, height, by, dst + by * rowBytes);
                    }
                });
            }
        }
    }

    error = pkg.validate();
    if (!error.empty()) return std::nullopt;
    return pkg;
}

}
