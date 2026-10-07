#include "MaterialPackage.h"

#include <algorithm>
#include <cstring>

#include "common/BinaryStream.h"

namespace ailo::material {

namespace {

using binary::makeTag;
using binary::Reader;
using binary::Writer;

// Package layout: "AMAT" | u32 version | chunks... (see common/BinaryStream.h)
constexpr uint32_t kMagic     = makeTag("AMAT");
constexpr uint32_t kChunkInfo = makeTag("INFO");
constexpr uint32_t kChunkVars = makeTag("VARS");
constexpr uint32_t kChunkParm = makeTag("PARM");
constexpr uint32_t kChunkShdr = makeTag("SHDR");
constexpr uint32_t kChunkBlob = makeTag("BLOB");

}

const MaterialParameter* MaterialPackage::findParameter(std::string_view name) const {
    auto it = std::ranges::find(parameters, name, &MaterialParameter::name);
    return it != parameters.end() ? &*it : nullptr;
}

std::span<const uint32_t> MaterialPackage::getShader(ShaderStage stage, Variant variant) const {
    Variant filtered = stage == ShaderStage::Vertex ? Variant::filterVertex(variant) : Variant::filterFragment(variant);
    for (const auto& entry : shaders) {
        if (entry.stage == stage && entry.variant == filtered) {
            return blobs[entry.blobIndex];
        }
    }
    return {};
}

std::vector<uint8_t> MaterialPackage::serialize() const {
    Writer w;
    w.u32(kMagic);
    w.u32(kMaterialPackageVersion);

    w.beginChunk(kChunkInfo);
    w.string(name);
    w.u8(uint8_t(shadingModel));
    w.u8(uint8_t(blending));
    w.u8(uint8_t(vertexDomain));
    w.u8(uint8_t(raster.culling));
    w.boolean(raster.colorWrite);
    w.boolean(raster.depthWrite);
    w.boolean(raster.depthCulling);
    w.boolean(raster.doubleSided);
    w.u8(uint8_t(raster.depthFunc));
    w.u8(requiredAttributes);
    w.u8(variantFilter);
    w.endChunk();

    w.beginChunk(kChunkVars);
    w.u32(uint32_t(variables.size()));
    for (const auto& v : variables) w.string(v);
    w.endChunk();

    w.beginChunk(kChunkParm);
    w.u32(uniformBlockSize);
    w.u32(uint32_t(parameters.size()));
    for (const auto& p : parameters) {
        w.string(p.name);
        w.u8(uint8_t(p.type));
        w.u32(p.arraySize);
        w.u32(p.offset);
        w.u32(p.size);
        w.u32(uint32_t(p.defaultValue.size()));
        w.bytes(p.defaultValue.data(), p.defaultValue.size());
        w.u32(p.binding);
        w.u8(uint8_t(p.samplerDefault));
    }
    w.endChunk();

    w.beginChunk(kChunkShdr);
    w.u32(uint32_t(shaders.size()));
    for (const auto& s : shaders) {
        w.u8(uint8_t(s.stage));
        w.u8(s.variant.key);
        w.u32(s.blobIndex);
    }
    w.endChunk();

    w.beginChunk(kChunkBlob);
    w.u32(uint32_t(blobs.size()));
    for (const auto& b : blobs) {
        w.u32(uint32_t(b.size()));
        w.bytes(b.data(), b.size() * sizeof(uint32_t));
    }
    w.endChunk();

    return w.take();
}

std::optional<MaterialPackage> MaterialPackage::deserialize(std::span<const uint8_t> data, std::string& error) {
    Reader header(data);
    if (header.u32() != kMagic || !header.ok()) {
        error = "not a material package";
        return std::nullopt;
    }
    uint32_t version = header.u32();
    if (version != kMaterialPackageVersion) {
        error = "material package version " + std::to_string(version) + ", engine expects " +
                std::to_string(kMaterialPackageVersion) + " (rebuild the materials)";
        return std::nullopt;
    }

    MaterialPackage pkg;
    auto chunks = data.subspan(2 * sizeof(uint32_t));
    Reader r(chunks);

    while (r.ok() && !r.atEnd()) {
        uint32_t tag = r.u32();
        uint32_t size = r.u32();
        Reader c(r.sub(size));

        switch (tag) {
            case kChunkInfo:
                pkg.name = c.string();
                pkg.shadingModel = c.enumeration(ShadingModel::Unlit);
                pkg.blending = c.enumeration(BlendingMode::Opaque);
                pkg.vertexDomain = c.enumeration(VertexDomain::Object);
                pkg.raster.culling = c.enumeration(CullingMode::FrontAndBack);
                pkg.raster.colorWrite = c.boolean();
                pkg.raster.depthWrite = c.boolean();
                pkg.raster.depthCulling = c.boolean();
                pkg.raster.doubleSided = c.boolean();
                pkg.raster.depthFunc = c.enumeration(DepthFunc::Always);
                pkg.requiredAttributes = c.u8();
                pkg.variantFilter = c.u8();
                break;

            case kChunkVars: {
                uint32_t count = c.u32();
                for (uint32_t i = 0; i < count && c.ok(); i++) {
                    pkg.variables.push_back(c.string());
                }
                break;
            }

            case kChunkParm: {
                pkg.uniformBlockSize = c.u32();
                uint32_t count = c.u32();
                for (uint32_t i = 0; i < count && c.ok(); i++) {
                    auto& p = pkg.parameters.emplace_back();
                    p.name = c.string();
                    p.type = c.enumeration(ParameterType::SamplerCubemap);
                    p.arraySize = c.u32();
                    p.offset = c.u32();
                    p.size = c.u32();
                    auto bytes = c.sub(c.u32());
                    p.defaultValue.assign(bytes.begin(), bytes.end());
                    p.binding = c.u32();
                    p.samplerDefault = c.enumeration(SamplerDefault::Normal);
                }
                break;
            }

            case kChunkShdr: {
                uint32_t count = c.u32();
                for (uint32_t i = 0; i < count && c.ok(); i++) {
                    auto stage = c.enumeration(ShaderStage::Fragment);
                    auto variant = Variant { c.u8() };
                    auto blobIndex = c.u32();
                    pkg.shaders.push_back({ stage, variant, blobIndex });
                }
                break;
            }

            case kChunkBlob: {
                uint32_t count = c.u32();
                for (uint32_t i = 0; i < count && c.ok(); i++) {
                    uint32_t words = c.u32();
                    auto bytes = c.sub(size_t(words) * sizeof(uint32_t));
                    auto& blob = pkg.blobs.emplace_back(words);
                    if (!bytes.empty()) std::memcpy(blob.data(), bytes.data(), bytes.size());
                }
                break;
            }

            default:
                break;
        }

        if (!c.ok()) {
            error = "corrupt material package";
            return std::nullopt;
        }
    }

    if (!r.ok()) {
        error = "truncated material package";
        return std::nullopt;
    }

    for (const auto& s : pkg.shaders) {
        if (s.blobIndex >= pkg.blobs.size()) {
            error = "material package references a missing shader blob";
            return std::nullopt;
        }
    }

    return pkg;
}

}
