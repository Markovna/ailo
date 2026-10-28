#include "LightingPackage.h"

#include <cmath>

#include "common/BinaryStream.h"

namespace ailo::lighting {

namespace {

using binary::makeTag;
using binary::Reader;
using binary::Writer;

// Package layout: "ALIT" | u32 version | chunks... (see common/BinaryStream.h). The SH and the reflections cubemap
// are one chunk each.
constexpr uint32_t kMagic             = makeTag("ALIT");
constexpr uint32_t kChunkIrradianceSH = makeTag("SHIR");
constexpr uint32_t kChunkReflections  = makeTag("TXTR");

}

std::string LightingPackage::validate() const {
    for (const auto& c : irradianceSH) {
        if (!std::isfinite(c.x) || !std::isfinite(c.y) || !std::isfinite(c.z)) {
            return "irradiance SH has a non-finite coefficient";
        }
    }
    if (reflections.type != TextureType::Cubemap) {
        return "reflections must be a cubemap";
    }
    if (auto error = reflections.validate(); !error.empty()) {
        return "reflections: " + error;
    }
    return {};
}

std::vector<uint8_t> LightingPackage::serialize() const {
    Writer w;
    w.u32(kMagic);
    w.u32(kLightingPackageVersion);

    w.beginChunk(kChunkIrradianceSH);
    w.u32(kIrradianceSHCount);
    for (const auto& c : irradianceSH) {
        w.f32(c.x); w.f32(c.y); w.f32(c.z);
    }
    w.endChunk();

    auto data = reflections.serialize();
    w.beginChunk(kChunkReflections);
    w.bytes(data.data(), data.size());
    w.endChunk();

    return w.take();
}

std::optional<LightingPackage> LightingPackage::deserialize(std::span<const uint8_t> data, std::string& error) {
    Reader header(data);
    if (header.u32() != kMagic || !header.ok()) {
        error = "not a lighting package (an uncompiled .light file?)";
        return std::nullopt;
    }
    uint32_t version = header.u32();
    if (version != kLightingPackageVersion) {
        error = "lighting package version " + std::to_string(version) + ", engine expects " +
                std::to_string(kLightingPackageVersion) + " (rebuild the assets)";
        return std::nullopt;
    }

    LightingPackage pkg;
    bool hasSH = false;
    bool hasReflections = false;
    Reader r(data.subspan(2 * sizeof(uint32_t)));

    while (r.ok() && !r.atEnd()) {
        uint32_t tag = r.u32();
        uint32_t size = r.u32();
        auto payload = r.sub(size);
        if (!r.ok()) break;
        Reader c(payload);

        switch (tag) {
            case kChunkIrradianceSH: {
                uint32_t count = c.u32();
                if (count != kIrradianceSHCount) {
                    error = "irradiance SH has " + std::to_string(count) + " coefficients, expected " +
                            std::to_string(kIrradianceSHCount);
                    return std::nullopt;
                }
                for (auto& coefficient : pkg.irradianceSH) {
                    coefficient.x = c.f32(); coefficient.y = c.f32(); coefficient.z = c.f32();
                }
                hasSH = true;
                break;
            }

            case kChunkReflections: {
                std::string textureError;
                auto texture = texture::TexturePackage::deserialize(payload, textureError);
                if (!texture) {
                    error = "reflections: " + textureError;
                    return std::nullopt;
                }
                pkg.reflections = std::move(*texture);
                hasReflections = true;
                break;
            }

            default:
                break;
        }

        if (!c.ok()) {
            error = "corrupt lighting package";
            return std::nullopt;
        }
    }

    if (!r.ok()) {
        error = "truncated lighting package";
        return std::nullopt;
    }
    if (!hasSH || !hasReflections) {
        error = !hasSH ? "lighting package has no irradiance SH" : "lighting package has no reflections";
        return std::nullopt;
    }

    error = pkg.validate();
    if (!error.empty()) {
        return std::nullopt;
    }
    return pkg;
}

}
