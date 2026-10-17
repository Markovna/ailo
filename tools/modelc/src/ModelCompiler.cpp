#include "ModelCompiler.h"

#include <assimp/DefaultIOSystem.h>
#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <meshoptimizer.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include "render/texture/TexturePacker.h"

namespace fs = std::filesystem;
using namespace ailo;
using namespace ailo::model;

namespace modelc {

namespace {

// Matches textures::kDefaultMetallicRoughness (render/DefaultAssetFactory.h), which modelc can't include.
constexpr const char* kDefaultMetallicRoughness = "builtin://textures/default_metallic_roughness";

constexpr unsigned kMaxVertices = 65535;

// meshopt_optimizeOverdraw may make the vertex cache this much worse to reduce overdraw (its recommended value).
constexpr float kOverdrawThreshold = 1.05f;

static_assert(offsetof(StaticVertex, position) == 0 && offsetof(SkinnedVertex, position) == 0);

fs::path fromUtf8(const std::string& s) {
    return fs::path(std::u8string(s.begin(), s.end()));
}

std::string toUtf8(const fs::path& p) {
    auto s = p.generic_u8string();
    return std::string(s.begin(), s.end());
}

// Records every file assimp opens (the source plus .bin buffers, .mtl libraries, ...) for the depfile.
// Wraps DefaultIOSystem, which is final.
class RecordingIOSystem : public Assimp::IOSystem {
public:
    explicit RecordingIOSystem(std::vector<fs::path>& opened) : m_opened(opened) {}

    bool Exists(const char* file) const override { return m_io.Exists(file); }
    char getOsSeparator() const override { return m_io.getOsSeparator(); }
    bool ComparePaths(const char* a, const char* b) const override { return m_io.ComparePaths(a, b); }
    void Close(Assimp::IOStream* stream) override { m_io.Close(stream); }

    Assimp::IOStream* Open(const char* file, const char* mode) override {
        Assimp::IOStream* stream = m_io.Open(file, mode);
        if (stream) m_opened.push_back(fs::weakly_canonical(fromUtf8(file)));
        return stream;
    }

private:
    Assimp::DefaultIOSystem m_io;
    std::vector<fs::path>& m_opened;
};

glm::mat4 toGlm(const aiMatrix4x4& m) {
    return glm::mat4(
        m.a1, m.b1, m.c1, m.d1,
        m.a2, m.b2, m.c2, m.d2,
        m.a3, m.b3, m.c3, m.d3,
        m.a4, m.b4, m.c4, m.d4
    );
}

// glTF URIs are RFC 3986 references, so "my%20texture.png" names "my texture.png".
std::string percentDecode(const std::string& s) {
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size() && hex(s[i + 1]) >= 0 && hex(s[i + 2]) >= 0) {
            out += char(hex(s[i + 1]) * 16 + hex(s[i + 2]));
            i += 2;
        } else {
            out += s[i];
        }
    }
    return out;
}

class Compiler {
public:
    Compiler(const ModelDefinition& def, const fs::path& definitionDir, CompileOutput& out)
        : m_def(def), m_definitionDir(definitionDir), m_out(out), m_pkg(out.package) {}

    void compile() {
        const fs::path sourcePath = m_definitionDir / m_def.source;
        std::string ext = toUtf8(m_def.source.extension());
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        m_percentEncodedUris = ext == ".gltf" || ext == ".glb";

        Assimp::Importer importer;
        importer.SetIOHandler(new RecordingIOSystem(m_out.dependencies));
        importer.SetPropertyInteger(AI_CONFIG_PP_SLM_VERTEX_LIMIT, kMaxVertices);
        importer.SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE, aiPrimitiveType_POINT | aiPrimitiveType_LINE);

        m_scene = importer.ReadFile(toUtf8(sourcePath),
            aiProcess_Triangulate |
            aiProcess_SortByPType |
            aiProcess_JoinIdenticalVertices |
            aiProcess_SplitLargeMeshes |
            aiProcess_OptimizeMeshes |
            aiProcess_FlipUVs |
            aiProcess_GenNormals |
            aiProcess_CalcTangentSpace |
            aiProcess_LimitBoneWeights);

        if (!m_scene || (m_scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !m_scene->mRootNode) {
            throw CompileError("cannot import '" + toUtf8(sourcePath) + "': " + importer.GetErrorString());
        }

        buildSkeleton();
        buildMaterials();
        buildMeshes();
        if (m_def.optimize) {
            for (Mesh& mesh : m_pkg.meshes) optimizeMesh(mesh);
        }
        buildClips();
        buildInstances(m_scene->mRootNode, glm::mat4(1.0f));
    }

private:
    void buildSkeleton() {
        for (unsigned i = 0; i < m_scene->mNumMeshes; i++) {
            const aiMesh* mesh = m_scene->mMeshes[i];
            for (unsigned j = 0; j < mesh->mNumBones; j++) m_boneNames.insert(mesh->mBones[j]->mName.C_Str());
        }
        if (m_boneNames.empty()) return;

        // Bones and all their ancestors, so that intermediate node transforms (e.g. "Armature") are accumulated into
        // the bones' world transforms: inverseBindPose was computed from the full hierarchy.
        std::unordered_set<std::string> included;
        std::function<bool(const aiNode*)> markAncestors = [&](const aiNode* node) {
            bool anyChildIncluded = false;
            for (unsigned i = 0; i < node->mNumChildren; i++) {
                if (markAncestors(node->mChildren[i])) anyChildIncluded = true;
            }
            std::string name = node->mName.C_Str();
            if (m_boneNames.contains(name) || anyChildIncluded) {
                included.insert(name);
                return true;
            }
            return false;
        };
        markAncestors(m_scene->mRootNode);

        std::unordered_map<std::string, uint32_t> nodeIndices;
        int32_t nextBoneOutput = 0;
        std::function<void(const aiNode*, int32_t)> addNodes = [&](const aiNode* node, int32_t parent) {
            std::string name = node->mName.C_Str();
            if (!included.contains(name)) return;

            auto index = int32_t(m_pkg.skeleton.size());
            nodeIndices[name] = index;
            SkeletonNode& n = m_pkg.skeleton.emplace_back();
            n.name = name;
            n.parent = parent;
            n.localTransform = toGlm(node->mTransformation);
            if (m_boneNames.contains(name)) {
                n.boneOutputIndex = nextBoneOutput;
                m_boneOutputs[name] = nextBoneOutput++;
            }

            for (unsigned i = 0; i < node->mNumChildren; i++) addNodes(node->mChildren[i], index);
        };
        addNodes(m_scene->mRootNode, -1);

        for (unsigned i = 0; i < m_scene->mNumMeshes; i++) {
            const aiMesh* mesh = m_scene->mMeshes[i];
            for (unsigned j = 0; j < mesh->mNumBones; j++) {
                const aiBone* bone = mesh->mBones[j];
                if (auto it = nodeIndices.find(bone->mName.C_Str()); it != nodeIndices.end()) {
                    m_pkg.skeleton[it->second].inverseBindPose = toGlm(bone->mOffsetMatrix);
                }
            }
        }
    }

    void buildMaterials() {
        std::set<std::string> overridden;
        for (unsigned i = 0; i < m_scene->mNumMaterials; i++) {
            const aiMaterial* mat = m_scene->mMaterials[i];
            Material& material = m_pkg.materials.emplace_back();
            material.name = mat->GetName().C_Str();
            if (auto it = m_def.materials.find(material.name); it != m_def.materials.end()) {
                material.material = it->second;
                overridden.insert(material.name);
            } else {
                material.material = m_def.material;
            }

            auto baseColor = texture(mat, aiTextureType_BASE_COLOR, true, material.name);
            if (!baseColor) baseColor = texture(mat, aiTextureType_DIFFUSE, true, material.name);
            auto normal = texture(mat, aiTextureType_NORMALS, false, material.name);
            auto metallicRoughness = texture(mat, aiTextureType_GLTF_METALLIC_ROUGHNESS, false, material.name);

            if (baseColor) material.textures.push_back({ "baseColorMap", *baseColor });
            if (normal) material.textures.push_back({ "normalMap", *normal });
            if (metallicRoughness) material.textures.push_back({ "metallicRoughnessMap", *metallicRoughness });

            aiColor4D color;
            if (mat->Get(AI_MATKEY_BASE_COLOR, color) == AI_SUCCESS) {
                material.floats.push_back({ "baseColorFactor", 4, { color.r, color.g, color.b, color.a } });
            }

            // Assimp reports a metallic factor only for metallic/roughness materials (glTF). For other formats it may
            // still derive a roughness factor (FBX: from shininess), which is ignored along with the glTF defaults
            // (fully metallic and rough) in favor of the engine's default metallic/roughness texture.
            float metallic = 1.0f, roughness = 1.0f;
            if (mat->Get(AI_MATKEY_METALLIC_FACTOR, metallic) == AI_SUCCESS) {
                material.floats.push_back({ "metallicFactor", 1, { metallic } });
                if (mat->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness) == AI_SUCCESS) {
                    material.floats.push_back({ "roughnessFactor", 1, { roughness } });
                }
            } else if (!metallicRoughness) {
                material.textures.push_back({ "metallicRoughnessMap", { TextureSource::Key, kDefaultMetallicRoughness, 0 } });
            }
        }

        for (const auto& [name, key] : m_def.materials) {
            if (overridden.contains(name)) continue;
            std::string available;
            for (const auto& material : m_pkg.materials) available += (available.empty() ? "" : ", ") + material.name;
            throw CompileError("materials: the source has no material named \"" + name + "\" (it has: " + available + ")");
        }
    }

    std::optional<TextureRef> texture(const aiMaterial* mat, aiTextureType type, bool srgb, const std::string& materialName) {
        aiString path;
        if (mat->GetTextureCount(type) == 0 || mat->GetTexture(type, 0, &path) != AI_SUCCESS) return std::nullopt;

        const std::string what = "material \"" + materialName + "\" " + aiTextureTypeToString(type) + " texture '" +
                                 path.C_Str() + "'";

        if (auto [embedded, index] = m_scene->GetEmbeddedTextureAndIndex(path.C_Str()); embedded) {
            return TextureRef { TextureSource::Embedded, {}, embeddedTexture(embedded, srgb, what) };
        }

        fs::path uri = fromUtf8(m_percentEncodedUris ? percentDecode(path.C_Str()) : path.C_Str());
        if (uri.has_root_name() || uri.has_root_directory()) {
            throw CompileError(what + ": absolute paths are not supported, reference the texture relative to the model");
        }

        // Relative to the .model file, which the runtime resolves against the model's asset key.
        fs::path relative = (m_def.source.parent_path() / uri).lexically_normal();
        return TextureRef { TextureSource::Uri, toUtf8(relative), 0 };
    }

    uint32_t embeddedTexture(const aiTexture* tex, bool srgb, const std::string& what) {
        auto key = std::make_pair(tex, srgb);
        if (auto it = m_embedded.find(key); it != m_embedded.end()) return it->second;

        texture::TextureImportOptions options { .srgb = srgb, .mipmaps = texture::MipmapMode::Runtime };
        std::string error;
        // mHeight == 0: pcData holds mWidth bytes of an encoded image (png, jpg, ...); otherwise raw BGRA texels.
        auto pkg = tex->mHeight == 0
            ? texture::packTextureFromMemory({ reinterpret_cast<const uint8_t*>(tex->pcData), tex->mWidth }, options, error)
            : texture::packTextureFromPixels(tex->pcData, tex->mWidth, tex->mHeight, texture::PixelLayout::BGRA8, options, error);
        if (!pkg) throw CompileError(what + ": embedded texture: " + error);

        auto index = uint32_t(m_pkg.textures.size());
        m_pkg.textures.push_back(std::move(*pkg));
        m_embedded.emplace(key, index);
        return index;
    }

    template<typename V>
    static V vertex(const aiMesh* mesh, unsigned v) {
        V vx {};
        vx.position = { mesh->mVertices[v].x, mesh->mVertices[v].y, mesh->mVertices[v].z };
        vx.texCoord = mesh->mTextureCoords[0]
            ? glm::vec2(mesh->mTextureCoords[0][v].x, mesh->mTextureCoords[0][v].y)
            : glm::vec2(0.0f);
        vx.color = mesh->mColors[0]
            ? glm::vec3(mesh->mColors[0][v].r, mesh->mColors[0][v].g, mesh->mColors[0][v].b)
            : glm::vec3(1.0f);
        if (mesh->mNormals) vx.normal = { mesh->mNormals[v].x, mesh->mNormals[v].y, mesh->mNormals[v].z };
        if (mesh->mTangents) {
            glm::vec3 t(mesh->mTangents[v].x, mesh->mTangents[v].y, mesh->mTangents[v].z);
            glm::vec3 b(mesh->mBitangents[v].x, mesh->mBitangents[v].y, mesh->mBitangents[v].z);
            vx.tangent = glm::vec4(t, glm::dot(glm::cross(vx.normal, t), b));
        } else {
            vx.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
        }
        return vx;
    }

    template<typename V>
    static void storeVertices(Mesh& mesh, const std::vector<V>& vertices) {
        mesh.vertexCount = uint32_t(vertices.size());
        mesh.vertices.resize(vertices.size() * sizeof(V));
        std::memcpy(mesh.vertices.data(), vertices.data(), mesh.vertices.size());
    }

    void buildMeshes() {
        for (unsigned i = 0; i < m_scene->mNumMeshes; i++) {
            const aiMesh* src = m_scene->mMeshes[i];
            const std::string what = "mesh \"" + std::string(src->mName.C_Str()) + "\"";
            if (src->mNumVertices > kMaxVertices + 1) {
                throw CompileError(what + " has " + std::to_string(src->mNumVertices) +
                                   " vertices after splitting, 16-bit indices address 65536");
            }

            Mesh& mesh = m_pkg.meshes.emplace_back();
            mesh.indices.reserve(src->mNumFaces * 3);
            for (unsigned f = 0; f < src->mNumFaces; f++) {
                const aiFace& face = src->mFaces[f];
                if (face.mNumIndices != 3) continue;
                for (unsigned j = 0; j < 3; j++) mesh.indices.push_back(uint16_t(face.mIndices[j]));
            }
            if (mesh.indices.empty()) throw CompileError(what + " has no triangles");

            if (src->mNumBones == 0) {
                mesh.layout = VertexLayout::Static;
                std::vector<StaticVertex> vertices;
                vertices.reserve(src->mNumVertices);
                for (unsigned v = 0; v < src->mNumVertices; v++) vertices.push_back(vertex<StaticVertex>(src, v));
                storeVertices(mesh, vertices);
                continue;
            }

            std::vector<std::vector<std::pair<int32_t, float>>> weights(src->mNumVertices);
            for (unsigned j = 0; j < src->mNumBones; j++) {
                const aiBone* bone = src->mBones[j];
                auto it = m_boneOutputs.find(bone->mName.C_Str());
                if (it == m_boneOutputs.end()) continue;
                for (unsigned k = 0; k < bone->mNumWeights; k++) {
                    weights[bone->mWeights[k].mVertexId].push_back({ it->second, bone->mWeights[k].mWeight });
                }
            }

            mesh.layout = VertexLayout::Skinned;
            std::vector<SkinnedVertex> vertices;
            vertices.reserve(src->mNumVertices);
            for (unsigned v = 0; v < src->mNumVertices; v++) {
                SkinnedVertex sv = vertex<SkinnedVertex>(src, v);
                const auto& bws = weights[v];
                for (size_t k = 0; k < std::min<size_t>(4, bws.size()); k++) {
                    sv.boneIndices[int(k)] = bws[k].first;
                    sv.boneWeights[int(k)] = bws[k].second;
                }
                vertices.push_back(sv);
            }
            storeVertices(mesh, vertices);
        }
    }

    // Merges vertices that are identical in the engine's layout (assimp compares its own attributes, some of which are
    // dropped or defaulted here), then orders triangles for the post-transform cache and overdraw, and vertices by
    // first use. Overdraw reads positions at offset 0 of the vertex (asserted at the top).
    static void optimizeMesh(Mesh& mesh) {
        const size_t stride = vertexStride(mesh.layout);
        const size_t indexCount = mesh.indices.size();

        std::vector<unsigned> remap(mesh.vertexCount);
        const size_t vertexCount = meshopt_generateVertexRemap(remap.data(), mesh.indices.data(), indexCount,
                                                               mesh.vertices.data(), mesh.vertexCount, stride);
        std::vector<uint8_t> vertices(vertexCount * stride);
        meshopt_remapVertexBuffer(vertices.data(), mesh.vertices.data(), mesh.vertexCount, stride, remap.data());
        meshopt_remapIndexBuffer(mesh.indices.data(), mesh.indices.data(), indexCount, remap.data());

        meshopt_optimizeVertexCache(mesh.indices.data(), mesh.indices.data(), indexCount, vertexCount);
        meshopt_optimizeOverdraw(mesh.indices.data(), mesh.indices.data(), indexCount,
                                 reinterpret_cast<const float*>(vertices.data()), vertexCount, stride, kOverdrawThreshold);

        mesh.vertices.resize(vertexCount * stride);
        mesh.vertexCount = uint32_t(meshopt_optimizeVertexFetch(mesh.vertices.data(), mesh.indices.data(), indexCount,
                                                                vertices.data(), vertexCount, stride));
        mesh.vertices.resize(size_t(mesh.vertexCount) * stride);
    }

    void buildClips() {
        if (m_boneNames.empty()) return;

        for (unsigned i = 0; i < m_scene->mNumAnimations; i++) {
            const aiAnimation* anim = m_scene->mAnimations[i];
            Clip& clip = m_pkg.clips.emplace_back();
            clip.name = anim->mName.C_Str();
            clip.ticksPerSecond = anim->mTicksPerSecond > 0.0 ? float(anim->mTicksPerSecond) : 25.0f;
            clip.duration = float(anim->mDuration) / clip.ticksPerSecond;

            for (unsigned j = 0; j < anim->mNumChannels; j++) {
                const aiNodeAnim* ch = anim->mChannels[j];
                Channel& channel = clip.channels.emplace_back();
                channel.node = ch->mNodeName.C_Str();
                auto seconds = [&](double ticks) { return float(ticks) / clip.ticksPerSecond; };
                for (unsigned k = 0; k < ch->mNumPositionKeys; k++) {
                    const auto& key = ch->mPositionKeys[k];
                    channel.positions.push_back({ seconds(key.mTime), { key.mValue.x, key.mValue.y, key.mValue.z } });
                }
                for (unsigned k = 0; k < ch->mNumRotationKeys; k++) {
                    const auto& key = ch->mRotationKeys[k];
                    channel.rotations.push_back({ seconds(key.mTime),
                                                  glm::quat(key.mValue.w, key.mValue.x, key.mValue.y, key.mValue.z) });
                }
                for (unsigned k = 0; k < ch->mNumScalingKeys; k++) {
                    const auto& key = ch->mScalingKeys[k];
                    channel.scales.push_back({ seconds(key.mTime), { key.mValue.x, key.mValue.y, key.mValue.z } });
                }
            }
        }
    }

    void buildInstances(const aiNode* node, const glm::mat4& parentTransform) {
        glm::mat4 transform = parentTransform * toGlm(node->mTransformation);
        for (unsigned i = 0; i < node->mNumMeshes; i++) {
            unsigned meshIndex = node->mMeshes[i];
            m_pkg.instances.push_back({ meshIndex, m_scene->mMeshes[meshIndex]->mMaterialIndex, transform });
        }
        for (unsigned i = 0; i < node->mNumChildren; i++) buildInstances(node->mChildren[i], transform);
    }

    const ModelDefinition& m_def;
    const fs::path& m_definitionDir;
    CompileOutput& m_out;
    ModelPackage& m_pkg;

    const aiScene* m_scene = nullptr;
    bool m_percentEncodedUris = false;
    std::unordered_set<std::string> m_boneNames;
    std::unordered_map<std::string, int32_t> m_boneOutputs;     // bone name -> SkeletonNode::boneOutputIndex
    std::map<std::pair<const aiTexture*, bool>, uint32_t> m_embedded;   // (texture, srgb) -> package texture
};

}

CompileOutput compileModel(const ModelDefinition& def, const fs::path& definitionDir) {
    CompileOutput out;
    Compiler(def, definitionDir, out).compile();

    if (auto error = out.package.validate(); !error.empty()) {
        throw CompileError("invalid package: " + error);
    }

    std::sort(out.dependencies.begin(), out.dependencies.end());
    out.dependencies.erase(std::unique(out.dependencies.begin(), out.dependencies.end()), out.dependencies.end());
    return out;
}

}
