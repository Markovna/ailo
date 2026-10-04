#include "MaterialPackage.h"

#include <algorithm>
#include <cstring>

namespace ailo::material {

namespace {

// Package layout:
//   "AMAT" | u32 version | chunks...
//   chunk: u32 tag | u32 size | payload (size bytes)
// Unknown chunks are skipped, so chunks can be added without breaking readers.
constexpr uint32_t makeTag(const char (&s)[5]) {
    return uint32_t(s[0]) | uint32_t(s[1]) << 8 | uint32_t(s[2]) << 16 | uint32_t(s[3]) << 24;
}

constexpr uint32_t kMagic     = makeTag("AMAT");
constexpr uint32_t kChunkInfo = makeTag("INFO");
constexpr uint32_t kChunkVars = makeTag("VARS");
constexpr uint32_t kChunkParm = makeTag("PARM");
constexpr uint32_t kChunkShdr = makeTag("SHDR");
constexpr uint32_t kChunkBlob = makeTag("BLOB");

class Writer {
public:
    void u8(uint8_t v) { m_data.push_back(v); }
    void u32(uint32_t v) { bytes(&v, sizeof(v)); }
    void boolean(bool v) { u8(v ? 1 : 0); }
    void string(const std::string& s) { u32(uint32_t(s.size())); bytes(s.data(), s.size()); }
    void bytes(const void* p, size_t n) {
        auto* b = static_cast<const uint8_t*>(p);
        m_data.insert(m_data.end(), b, b + n);
    }

    void beginChunk(uint32_t tag) {
        u32(tag);
        m_chunkSizeOffset = m_data.size();
        u32(0);
    }
    void endChunk() {
        uint32_t size = uint32_t(m_data.size() - m_chunkSizeOffset - sizeof(uint32_t));
        std::memcpy(m_data.data() + m_chunkSizeOffset, &size, sizeof(size));
    }

    std::vector<uint8_t> take() { return std::move(m_data); }

private:
    std::vector<uint8_t> m_data;
    size_t m_chunkSizeOffset = 0;
};

class Reader {
public:
    explicit Reader(std::span<const uint8_t> data) : m_data(data) {}

    bool ok() const { return m_ok; }
    bool atEnd() const { return m_pos >= m_data.size(); }

    uint8_t u8() { uint8_t v = 0; bytes(&v, 1); return v; }
    uint32_t u32() { uint32_t v = 0; bytes(&v, sizeof(v)); return v; }
    bool boolean() { return u8() != 0; }
    std::string string() {
        uint32_t n = u32();
        if (!check(n)) return {};
        std::string s(reinterpret_cast<const char*>(m_data.data() + m_pos), n);
        m_pos += n;
        return s;
    }
    void bytes(void* out, size_t n) {
        if (!check(n)) { std::memset(out, 0, n); return; }
        std::memcpy(out, m_data.data() + m_pos, n);
        m_pos += n;
    }
    std::span<const uint8_t> sub(size_t n) {
        if (!check(n)) return {};
        auto s = m_data.subspan(m_pos, n);
        m_pos += n;
        return s;
    }

    template<typename E>
    E enumeration(E maxValue) {
        uint8_t v = u8();
        if (v > uint8_t(maxValue)) m_ok = false;
        return E(v);
    }

private:
    bool check(size_t n) {
        if (!m_ok || m_data.size() - m_pos < n) {
            m_ok = false;
            return false;
        }
        return true;
    }

    std::span<const uint8_t> m_data;
    size_t m_pos = 0;
    bool m_ok = true;
};

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
