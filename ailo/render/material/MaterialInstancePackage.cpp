#include "MaterialInstancePackage.h"

#include <cstring>

#include "common/BinaryStream.h"

namespace ailo::material {

namespace {

using binary::makeTag;
using binary::Reader;
using binary::Writer;

// Package layout: "AMIN" | u32 version | chunks... (see common/BinaryStream.h)
constexpr uint32_t kMagic     = makeTag("AMIN");
constexpr uint32_t kChunkInfo = makeTag("INFO");
constexpr uint32_t kChunkParm = makeTag("PARM");
constexpr uint32_t kChunkText = makeTag("TEXT");

}

std::vector<uint8_t> MaterialInstancePackage::serialize() const {
    Writer w;
    w.u32(kMagic);
    w.u32(kMaterialInstancePackageVersion);

    w.beginChunk(kChunkInfo);
    w.string(material);
    w.endChunk();

    w.beginChunk(kChunkParm);
    w.u32(uint32_t(parameters.size()));
    for (const auto& p : parameters) {
        w.string(p.name);
        w.u8(uint8_t(p.type));
        w.u32(uint32_t(p.value.size()));
        w.bytes(p.value.data(), p.value.size() * sizeof(uint32_t));
    }
    w.endChunk();

    w.beginChunk(kChunkText);
    w.u32(uint32_t(textures.size()));
    for (const auto& t : textures) {
        w.string(t.name);
        w.string(t.texture);
    }
    w.endChunk();

    return w.take();
}

std::optional<MaterialInstancePackage> MaterialInstancePackage::deserialize(std::span<const uint8_t> data, std::string& error) {
    Reader header(data);
    if (header.u32() != kMagic || !header.ok()) {
        error = "not a material instance package (an uncompiled .matinst file?)";
        return std::nullopt;
    }
    uint32_t version = header.u32();
    if (version != kMaterialInstancePackageVersion) {
        error = "material instance package version " + std::to_string(version) + ", engine expects " +
                std::to_string(kMaterialInstancePackageVersion) + " (rebuild the material instances)";
        return std::nullopt;
    }

    MaterialInstancePackage pkg;
    auto chunks = data.subspan(2 * sizeof(uint32_t));
    Reader r(chunks);

    while (r.ok() && !r.atEnd()) {
        uint32_t tag = r.u32();
        uint32_t size = r.u32();
        Reader c(r.sub(size));

        switch (tag) {
            case kChunkInfo:
                pkg.material = c.string();
                break;

            case kChunkParm: {
                uint32_t count = c.u32();
                for (uint32_t i = 0; i < count && c.ok(); i++) {
                    auto& p = pkg.parameters.emplace_back();
                    p.name = c.string();
                    p.type = c.enumeration(ParameterType::SamplerCubemap);
                    uint32_t words = c.u32();
                    auto bytes = c.sub(size_t(words) * sizeof(uint32_t));
                    p.value.resize(bytes.size() / sizeof(uint32_t));
                    if (!bytes.empty()) std::memcpy(p.value.data(), bytes.data(), bytes.size());
                }
                break;
            }

            case kChunkText: {
                uint32_t count = c.u32();
                for (uint32_t i = 0; i < count && c.ok(); i++) {
                    auto& t = pkg.textures.emplace_back();
                    t.name = c.string();
                    t.texture = c.string();
                }
                break;
            }

            default:
                break;
        }

        if (!c.ok()) {
            error = "corrupt material instance package";
            return std::nullopt;
        }
    }

    if (!r.ok()) {
        error = "truncated material instance package";
        return std::nullopt;
    }

    return pkg;
}

}
