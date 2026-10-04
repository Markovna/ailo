#pragma once

#include <bit>
#include <span>
#include <vector>

#include <glm/glm.hpp>

#include "Material.h"
#include "Texture.h"

namespace ailo {

template<typename T>
struct UniformTraits;

template<> struct UniformTraits<float>    { static constexpr auto type = material::ParameterType::Float; };
template<> struct UniformTraits<int32_t>  { static constexpr auto type = material::ParameterType::Int; };
template<> struct UniformTraits<uint32_t> { static constexpr auto type = material::ParameterType::UInt; };
template<> struct UniformTraits<bool>     { static constexpr auto type = material::ParameterType::Bool; };

template<glm::length_t L, typename T, glm::qualifier Q>
struct UniformTraits<glm::vec<L, T, Q>> {
    static constexpr auto type = material::ParameterType(uint8_t(UniformTraits<T>::type) + L - 1);
};

template<glm::qualifier Q> struct UniformTraits<glm::mat<3, 3, float, Q>> { static constexpr auto type = material::ParameterType::Float3x3; };
template<glm::qualifier Q> struct UniformTraits<glm::mat<4, 4, float, Q>> { static constexpr auto type = material::ParameterType::Float4x4; };

template<typename T>
concept UniformValue = requires { UniformTraits<T>::type; };

// Parameter values of a Material: owns the MaterialParams uniform buffer and the set 2 descriptor set.
// Sampler parameters start with the built-in texture named by their default.
class MaterialInstance : public Asset {
public:
    MaterialInstance(RenderAPI*, AssetManager&, asset_ptr<Material>);

    const Material& getMaterial() const { return *m_material; }

    void setParameter(std::string_view name, asset_ptr<Texture> texture, const SamplerParams& sampler = {});

    template<UniformValue T>
    void setParameter(std::string_view name, const T& value) {
        setParameter(name, std::span<const T>(&value, 1));
    }

    // Writes consecutive elements of an array parameter, starting at firstElement.
    template<UniformValue T>
    void setParameter(std::string_view name, std::span<const T> values, uint32_t firstElement = 0) {
        std::vector<uint32_t> words;
        for (const T& value : values) {
            appendWords(value, words);
        }
        setUniform(name, UniformTraits<T>::type, words, firstElement);
    }

    void copyParametersFrom(const MaterialInstance& other);

    // Uploads changed parameters. Call while recording a frame, before drawing with the instance.
    void commit(RenderAPI&);
    void bind(RenderAPI&) const;

    static asset_ptr<MaterialInstance> create(AssetManager&, RenderAPI&, const asset_ptr<Material>&);

private:
    struct Sampler {
        uint32_t binding;
        asset_ptr<Texture> texture;
        SamplerParams params;
        bool dirty = true;
    };

    template<typename T>
    static void appendWords(const T& value, std::vector<uint32_t>& out) {
        if constexpr (std::is_same_v<T, bool>) {
            out.push_back(value ? 1u : 0u);
        } else if constexpr (std::is_arithmetic_v<T>) {
            out.push_back(std::bit_cast<uint32_t>(value));
        } else if constexpr (requires { value[0][0]; }) {
            for (glm::length_t c = 0; c < T::length(); c++) appendWords(value[c], out);
        } else {
            for (glm::length_t i = 0; i < T::length(); i++) appendWords(value[i], out);
        }
    }

    void setUniform(std::string_view name, material::ParameterType type, std::span<const uint32_t> words, uint32_t firstElement);

    asset_ptr<Material> m_material;
    std::vector<uint8_t> m_uniforms;
    bool m_uniformsDirty = true;
    Unique<gpu::Buffer> m_uniformBuffer;
    std::vector<Sampler> m_samplers;
    Unique<gpu::DescriptorSet> m_descriptorSet;
};

}
