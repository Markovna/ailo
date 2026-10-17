#include "ModelPackage.h"

#include <cstring>
#include <limits>

#include "common/BinaryStream.h"

namespace ailo::model {

namespace {

using binary::makeTag;
using binary::Reader;
using binary::Writer;

// Package layout: "AMDL" | u32 version | chunks... (see common/BinaryStream.h). Meshes, materials, clips and
// textures are one chunk each, in index order; instances and the skeleton are one chunk for all.
constexpr uint32_t kMagic         = makeTag("AMDL");
constexpr uint32_t kChunkMesh     = makeTag("MESH");
constexpr uint32_t kChunkMaterial = makeTag("MTRL");
constexpr uint32_t kChunkInstance = makeTag("INST");
constexpr uint32_t kChunkSkeleton = makeTag("SKEL");
constexpr uint32_t kChunkClip     = makeTag("CLIP");
constexpr uint32_t kChunkTexture  = makeTag("TXTR");

void writeVec3(Writer& w, const glm::vec3& v) {
    w.f32(v.x); w.f32(v.y); w.f32(v.z);
}

void writeQuat(Writer& w, const glm::quat& q) {
    w.f32(q.w); w.f32(q.x); w.f32(q.y); w.f32(q.z);
}

void writeMat4(Writer& w, const glm::mat4& m) {
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++) w.f32(m[c][r]);
}

glm::vec3 readVec3(Reader& r) {
    glm::vec3 v;
    v.x = r.f32(); v.y = r.f32(); v.z = r.f32();
    return v;
}

glm::quat readQuat(Reader& r) {
    float w = r.f32(), x = r.f32(), y = r.f32(), z = r.f32();
    return glm::quat(w, x, y, z);
}

glm::mat4 readMat4(Reader& r) {
    glm::mat4 m;
    for (int c = 0; c < 4; c++)
        for (int row = 0; row < 4; row++) m[c][row] = r.f32();
    return m;
}

void writeTextureRef(Writer& w, const TextureRef& t) {
    w.u8(uint8_t(t.source));
    w.string(t.path);
    w.u32(t.embeddedIndex);
}

TextureRef readTextureRef(Reader& r) {
    TextureRef t;
    t.source = r.enumeration(TextureSource::Key);
    t.path = r.string();
    t.embeddedIndex = r.u32();
    return t;
}

template<typename T, typename WriteValue>
void writeKeys(Writer& w, const std::vector<Key<T>>& keys, WriteValue writeValue) {
    w.u32(uint32_t(keys.size()));
    for (const auto& key : keys) {
        w.f32(key.time);
        writeValue(w, key.value);
    }
}

// Counts come from the file, so elements are appended one at a time instead of reserved up front: a corrupt count
// stops at the end of the chunk instead of allocating it.
template<typename T, typename ReadElement>
void readArray(Reader& r, std::vector<T>& out, ReadElement readElement) {
    uint32_t count = r.u32();
    for (uint32_t i = 0; i < count && r.ok(); i++) {
        out.push_back(readElement(r));
    }
}

template<typename T, typename ReadValue>
void readKeys(Reader& r, std::vector<Key<T>>& out, ReadValue readValue) {
    readArray(r, out, [&](Reader& r) {
        Key<T> key;
        key.time = r.f32();
        key.value = readValue(r);
        return key;
    });
}

std::string checkTexture(const TextureRef& t, size_t textureCount, const std::string& what) {
    switch (t.source) {
        case TextureSource::Uri:
        case TextureSource::Key:
            if (t.path.empty()) return what + " has an empty path";
            break;
        case TextureSource::Embedded:
            if (t.embeddedIndex >= textureCount) {
                return what + " uses embedded texture " + std::to_string(t.embeddedIndex) + ", the package has " +
                       std::to_string(textureCount);
            }
            break;
    }
    return {};
}

}

uint32_t vertexStride(VertexLayout layout) {
    return layout == VertexLayout::Skinned ? sizeof(SkinnedVertex) : sizeof(StaticVertex);
}

bool fitsUint16Indices(const Mesh& mesh) {
    return mesh.vertexCount <= std::numeric_limits<uint16_t>::max() + 1u;
}

std::string ModelPackage::validate() const {
    for (size_t i = 0; i < meshes.size(); i++) {
        const auto& mesh = meshes[i];
        const std::string what = "mesh " + std::to_string(i);
        if (mesh.vertices.size() != size_t(mesh.vertexCount) * vertexStride(mesh.layout)) {
            return what + " vertex data is " + std::to_string(mesh.vertices.size()) + " bytes, expected " +
                   std::to_string(size_t(mesh.vertexCount) * vertexStride(mesh.layout));
        }
        if (mesh.indices.empty() || mesh.indices.size() % 3 != 0) {
            return what + " has " + std::to_string(mesh.indices.size()) + " indices, expected a multiple of 3";
        }
        for (uint32_t index : mesh.indices) {
            if (index >= mesh.vertexCount) {
                return what + " index " + std::to_string(index) + " is out of range (" +
                       std::to_string(mesh.vertexCount) + " vertices)";
            }
        }
        if (mesh.layout == VertexLayout::Skinned && skeleton.empty()) {
            return what + " is skinned but the package has no skeleton";
        }
    }

    for (size_t i = 0; i < materials.size(); i++) {
        const auto& material = materials[i];
        const std::string what = "material " + std::to_string(i) + " (" + material.name + ")";
        if (material.material.empty()) return what + " names no material";
        for (const auto& p : material.floats) {
            if (p.components < 1 || p.components > 4) {
                return what + " parameter " + p.name + " has " + std::to_string(p.components) + " components";
            }
        }
        for (const auto& p : material.textures) {
            if (auto error = checkTexture(p.texture, textures.size(), what + " parameter " + p.name); !error.empty()) {
                return error;
            }
        }
    }

    for (size_t i = 0; i < instances.size(); i++) {
        const auto& instance = instances[i];
        if (instance.mesh >= meshes.size()) {
            return "instance " + std::to_string(i) + " uses mesh " + std::to_string(instance.mesh) + ", the package has " +
                   std::to_string(meshes.size());
        }
        if (instance.material >= materials.size()) {
            return "instance " + std::to_string(i) + " uses material " + std::to_string(instance.material) +
                   ", the package has " + std::to_string(materials.size());
        }
    }

    for (size_t i = 0; i < skeleton.size(); i++) {
        if (skeleton[i].parent >= int32_t(i) || skeleton[i].parent < -1) {
            return "skeleton node " + std::to_string(i) + " (" + skeleton[i].name + ") has parent " +
                   std::to_string(skeleton[i].parent) + ", parents must come first";
        }
    }

    for (size_t i = 0; i < textures.size(); i++) {
        if (auto error = textures[i].validate(); !error.empty()) {
            return "embedded texture " + std::to_string(i) + ": " + error;
        }
    }
    return {};
}

std::vector<uint8_t> ModelPackage::serialize() const {
    Writer w;
    w.u32(kMagic);
    w.u32(kModelPackageVersion);

    for (const auto& mesh : meshes) {
        w.beginChunk(kChunkMesh);
        w.u8(uint8_t(mesh.layout));
        w.u32(mesh.vertexCount);
        w.u32(uint32_t(mesh.vertices.size()));
        w.bytes(mesh.vertices.data(), mesh.vertices.size());
        w.u32(uint32_t(mesh.indices.size()));
        if (fitsUint16Indices(mesh)) {
            w.u8(sizeof(uint16_t));
            for (uint32_t index : mesh.indices) w.u16(uint16_t(index));
        } else {
            w.u8(sizeof(uint32_t));
            w.bytes(mesh.indices.data(), mesh.indices.size() * sizeof(uint32_t));
        }
        w.endChunk();
    }

    for (const auto& material : materials) {
        w.beginChunk(kChunkMaterial);
        w.string(material.name);
        w.string(material.material);
        w.u32(uint32_t(material.floats.size()));
        for (const auto& p : material.floats) {
            w.string(p.name);
            w.u8(uint8_t(p.components));
            for (uint32_t c = 0; c < p.components; c++) w.f32(p.value[c]);
        }
        w.u32(uint32_t(material.textures.size()));
        for (const auto& p : material.textures) {
            w.string(p.name);
            writeTextureRef(w, p.texture);
        }
        w.endChunk();
    }

    w.beginChunk(kChunkInstance);
    w.u32(uint32_t(instances.size()));
    for (const auto& instance : instances) {
        w.u32(instance.mesh);
        w.u32(instance.material);
        writeMat4(w, instance.transform);
    }
    w.endChunk();

    if (!skeleton.empty()) {
        w.beginChunk(kChunkSkeleton);
        w.u32(uint32_t(skeleton.size()));
        for (const auto& node : skeleton) {
            w.string(node.name);
            w.i32(node.parent);
            writeMat4(w, node.localTransform);
            writeMat4(w, node.inverseBindPose);
            w.i32(node.boneOutputIndex);
        }
        w.endChunk();
    }

    for (const auto& clip : clips) {
        w.beginChunk(kChunkClip);
        w.string(clip.name);
        w.f32(clip.ticksPerSecond);
        w.f32(clip.duration);
        w.u32(uint32_t(clip.channels.size()));
        for (const auto& channel : clip.channels) {
            w.string(channel.node);
            writeKeys(w, channel.positions, writeVec3);
            writeKeys(w, channel.rotations, writeQuat);
            writeKeys(w, channel.scales, writeVec3);
        }
        w.endChunk();
    }

    for (const auto& texture : textures) {
        auto data = texture.serialize();
        w.beginChunk(kChunkTexture);
        w.bytes(data.data(), data.size());
        w.endChunk();
    }

    return w.take();
}

std::optional<ModelPackage> ModelPackage::deserialize(std::span<const uint8_t> data, std::string& error) {
    Reader header(data);
    if (header.u32() != kMagic || !header.ok()) {
        error = "not a model package";
        return std::nullopt;
    }
    uint32_t version = header.u32();
    if (version != kModelPackageVersion) {
        error = "model package version " + std::to_string(version) + ", engine expects " +
                std::to_string(kModelPackageVersion) + " (rebuild the assets)";
        return std::nullopt;
    }

    ModelPackage pkg;
    Reader r(data.subspan(2 * sizeof(uint32_t)));

    while (r.ok() && !r.atEnd()) {
        uint32_t tag = r.u32();
        uint32_t size = r.u32();
        auto payload = r.sub(size);
        if (!r.ok()) break;
        Reader c(payload);

        switch (tag) {
            case kChunkMesh: {
                auto& mesh = pkg.meshes.emplace_back();
                mesh.layout = c.enumeration(VertexLayout::Skinned);
                mesh.vertexCount = c.u32();
                auto vertices = c.sub(c.u32());
                mesh.vertices.assign(vertices.begin(), vertices.end());
                uint32_t indexCount = c.u32();
                uint8_t indexSize = c.u8();
                if (indexSize == sizeof(uint16_t)) {
                    Reader indices(c.sub(size_t(indexCount) * sizeof(uint16_t)));
                    if (!c.ok()) break;
                    mesh.indices.resize(indexCount);
                    for (uint32_t& index : mesh.indices) index = indices.u16();
                } else if (indexSize == sizeof(uint32_t)) {
                    auto indices = c.sub(size_t(indexCount) * sizeof(uint32_t));
                    mesh.indices.resize(indices.size() / sizeof(uint32_t));
                    std::memcpy(mesh.indices.data(), indices.data(), indices.size());
                } else {
                    error = "mesh " + std::to_string(pkg.meshes.size() - 1) + " has " + std::to_string(indexSize) +
                            "-byte indices";
                    return std::nullopt;
                }
                break;
            }

            case kChunkMaterial: {
                auto& material = pkg.materials.emplace_back();
                material.name = c.string();
                material.material = c.string();
                readArray(c, material.floats, [](Reader& r) {
                    FloatParameter p;
                    p.name = r.string();
                    p.components = r.u8();
                    for (uint32_t i = 0; i < p.components && i < 4; i++) p.value[i] = r.f32();
                    return p;
                });
                readArray(c, material.textures, [](Reader& r) {
                    TextureParameter p;
                    p.name = r.string();
                    p.texture = readTextureRef(r);
                    return p;
                });
                break;
            }

            case kChunkInstance:
                readArray(c, pkg.instances, [](Reader& r) {
                    Instance instance;
                    instance.mesh = r.u32();
                    instance.material = r.u32();
                    instance.transform = readMat4(r);
                    return instance;
                });
                break;

            case kChunkSkeleton:
                readArray(c, pkg.skeleton, [](Reader& r) {
                    SkeletonNode node;
                    node.name = r.string();
                    node.parent = r.i32();
                    node.localTransform = readMat4(r);
                    node.inverseBindPose = readMat4(r);
                    node.boneOutputIndex = r.i32();
                    return node;
                });
                break;

            case kChunkClip: {
                auto& clip = pkg.clips.emplace_back();
                clip.name = c.string();
                clip.ticksPerSecond = c.f32();
                clip.duration = c.f32();
                readArray(c, clip.channels, [](Reader& r) {
                    Channel channel;
                    channel.node = r.string();
                    readKeys(r, channel.positions, readVec3);
                    readKeys(r, channel.rotations, readQuat);
                    readKeys(r, channel.scales, readVec3);
                    return channel;
                });
                break;
            }

            case kChunkTexture: {
                std::string textureError;
                auto texture = texture::TexturePackage::deserialize(payload, textureError);
                if (!texture) {
                    error = "embedded texture " + std::to_string(pkg.textures.size()) + ": " + textureError;
                    return std::nullopt;
                }
                pkg.textures.push_back(std::move(*texture));
                break;
            }

            default:
                break;
        }

        if (!c.ok()) {
            error = "corrupt model package";
            return std::nullopt;
        }
    }

    if (!r.ok()) {
        error = "truncated model package";
        return std::nullopt;
    }

    error = pkg.validate();
    if (!error.empty()) {
        return std::nullopt;
    }
    return pkg;
}

}
