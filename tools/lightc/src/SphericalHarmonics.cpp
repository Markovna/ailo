#include "SphericalHarmonics.h"

#include <cassert>
#include <cmath>
#include <cstring>
#include <vector>

#include <glm/gtc/constants.hpp>

#include "Cubemap.h"

namespace lightc {

using namespace ailo::texture;

namespace {

constexpr double PI = glm::pi<double>();

// Normalization constants of the real SH basis, per coefficient (index l * (l + 1) + m).
const std::array<double, 9> kBasisConstants = {
    0.5 * std::sqrt(1.0 / PI),
    std::sqrt(3.0 / (4.0 * PI)),
    std::sqrt(3.0 / (4.0 * PI)),
    std::sqrt(3.0 / (4.0 * PI)),
    0.5 * std::sqrt(15.0 / PI),
    0.5 * std::sqrt(15.0 / PI),
    0.25 * std::sqrt(5.0 / PI),
    0.5 * std::sqrt(15.0 / PI),
    0.25 * std::sqrt(15.0 / PI),
};

// Clamped cosine convolution (Ramamoorthi & Hanrahan 2001) divided by pi: A_l / pi for l = 0, 1, 2.
constexpr std::array<double, 9> kCosineLobe = {
    1.0,
    2.0 / 3.0, 2.0 / 3.0, 2.0 / 3.0,
    0.25, 0.25, 0.25, 0.25, 0.25,
};

// The basis without its constants; the order must match irradianceSH() in the lit surface shader.
template<typename T>
std::array<T, 9> basisPolynomials(const glm::vec<3, T>& n) {
    return {
        T(1),
        n.y,
        n.z,
        n.x,
        n.y * n.x,
        n.y * n.z,
        T(3) * n.z * n.z - T(1),
        n.z * n.x,
        n.x * n.x - n.y * n.y,
    };
}

// Integral of dA / |p|^3 over the face rectangle [-1, x] x [-1, y] at distance 1 (Driscoll 2012).
double areaElement(double x, double y) {
    return std::atan2(x * y, std::sqrt(x * x + y * y + 1.0));
}

double texelSolidAngle(uint32_t x, uint32_t y, uint32_t dim) {
    const double inv = 2.0 / dim;
    const double x0 = x * inv - 1.0, x1 = x0 + inv;
    const double y0 = y * inv - 1.0, y1 = y0 + inv;
    return areaElement(x0, y0) - areaElement(x0, y1) - areaElement(x1, y0) + areaElement(x1, y1);
}

}

IrradianceSH projectIrradianceSH(const TexturePackage& cubemap) {
    assert(cubemap.type == TextureType::Cubemap && cubemap.format == TextureFormat::RGBA32F);
    const uint32_t dim = cubemap.width;

    // The solid angle depends only on the texel position, the same on every face.
    std::vector<double> solidAngles(size_t(dim) * dim);
    for (uint32_t y = 0; y < dim; y++) {
        for (uint32_t x = 0; x < dim; x++) {
            solidAngles[size_t(y) * dim + x] = texelSolidAngle(x, y, dim);
        }
    }

    // Accumulated per row in double, so the sum over millions of texels stays exact and threads don't share state.
    std::vector<std::array<glm::dvec3, 9>> rows(size_t(6) * dim);

    #ifdef _OPENMP
    #pragma omp parallel for schedule(dynamic)
    #endif
    for (int row = 0; row < static_cast<int>(6 * dim); row++) {
        const int face = row / static_cast<int>(dim);
        const uint32_t y = uint32_t(row) % dim;
        const auto image = cubemap.image(0, uint32_t(face));
        auto& sum = rows[size_t(row)];
        sum.fill(glm::dvec3(0.0));

        for (uint32_t x = 0; x < dim; x++) {
            glm::vec4 texel;
            std::memcpy(&texel, image.data() + (size_t(y) * dim + x) * sizeof(glm::vec4), sizeof(glm::vec4));

            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(dim);
            const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(dim);
            const glm::dvec3 dir(cubemapToDirection(face, u, v));
            const glm::dvec3 radiance = glm::dvec3(glm::vec3(texel)) * solidAngles[size_t(y) * dim + x];

            const auto poly = basisPolynomials(dir);
            for (size_t i = 0; i < 9; i++) {
                sum[i] += radiance * poly[i];
            }
        }
    }

    std::array<glm::dvec3, 9> total{};
    for (const auto& row : rows) {
        for (size_t i = 0; i < 9; i++) total[i] += row[i];
    }

    // The projection integrates against K * poly; evaluating multiplies by K * poly again, so K appears squared.
    IrradianceSH sh;
    for (size_t i = 0; i < 9; i++) {
        sh[i] = glm::vec3(total[i] * (kBasisConstants[i] * kBasisConstants[i] * kCosineLobe[i]));
    }
    return sh;
}

glm::vec3 evaluateIrradianceSH(const IrradianceSH& sh, const glm::vec3& n) {
    const auto poly = basisPolynomials(n);
    glm::vec3 result(0.0f);
    for (size_t i = 0; i < 9; i++) {
        result += sh[i] * poly[i];
    }
    return result;
}

}
