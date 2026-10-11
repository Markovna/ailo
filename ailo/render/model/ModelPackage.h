#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "render/texture/TexturePackage.h"

namespace ailo::model {

// Bump whenever the binary layout or the meaning of any field changes; packages with another version are rejected.
static constexpr uint32_t kModelPackageVersion = 1;

// Vertices are stored in the layout the engine uploads; the attribute locations are VertexLocation's.
struct StaticVertex {
    glm::vec3 position;
    glm::vec3 color;
    glm::vec2 texCoord;
    glm::vec3 normal;
    glm::vec4 tangent;      // w: bitangent sign
};

struct SkinnedVertex {
    glm::vec3 position;
    glm::vec3 color;
    glm::vec2 texCoord;
    glm::vec3 normal;
    glm::vec4 tangent;
    glm::ivec4 boneIndices; // into the skeleton's bone outputs (SkeletonNode::boneOutputIndex)
    glm::vec4 boneWeights;
};

enum class VertexLayout : uint8_t {
    Static,
    Skinned,
};

uint32_t vertexStride(VertexLayout layout);

struct Mesh {
    VertexLayout layout = VertexLayout::Static;
    uint32_t vertexCount = 0;
    std::vector<uint8_t> vertices;      // vertexCount * vertexStride(layout) bytes
    std::vector<uint16_t> indices;      // triangle list
};

enum class TextureSource : uint8_t {
    Uri,        // `path` as written in the source model, relative to the model's folder
    Embedded,   // `embeddedIndex` into ModelPackage::textures
    Key,        // `path` is an asset key loaded as is (e.g. a builtin texture)
};

struct TextureRef {
    TextureSource source = TextureSource::Uri;
    std::string path;
    uint32_t embeddedIndex = 0;
};

struct TextureParameter {
    std::string name;
    TextureRef texture;
};

struct FloatParameter {
    std::string name;
    uint32_t components = 1;            // 1: float, 2..4: floatN
    std::array<float, 4> value {};
};

// Material is an asset key (e.g. "assets/materials/lit") that the runtime loads; parameters are set by name and
// skipped when the material does not declare them.
struct Material {
    std::string name;                   // as named in the source model
    std::string material;
    std::vector<FloatParameter> floats;
    std::vector<TextureParameter> textures;
};

struct Instance {
    uint32_t mesh = 0;
    uint32_t material = 0;
    glm::mat4 transform { 1.0f };       // relative to the model's root
};

struct SkeletonNode {
    std::string name;
    int32_t parent = -1;                // earlier in ModelPackage::skeleton, -1 for a root
    glm::mat4 localTransform { 1.0f };
    glm::mat4 inverseBindPose { 1.0f };
    int32_t boneOutputIndex = -1;       // -1: an ancestor of bones, not a bone itself
};

template<typename T>
struct Key {
    float time;                         // seconds
    T value;
};

struct Channel {
    std::string node;
    std::vector<Key<glm::vec3>> positions;
    std::vector<Key<glm::quat>> rotations;
    std::vector<Key<glm::vec3>> scales;
};

struct Clip {
    std::string name;
    float ticksPerSecond = 25.0f;
    float duration = 0.0f;              // seconds
    std::vector<Channel> channels;
};

struct ModelPackage {
    std::vector<Mesh> meshes;
    std::vector<Material> materials;
    std::vector<Instance> instances;
    std::vector<SkeletonNode> skeleton; // parent-first; empty if no mesh is skinned
    std::vector<Clip> clips;
    std::vector<texture::TexturePackage> textures;

    std::string validate() const;

    std::vector<uint8_t> serialize() const;

    static std::optional<ModelPackage> deserialize(std::span<const uint8_t> data, std::string& error);
};

}
