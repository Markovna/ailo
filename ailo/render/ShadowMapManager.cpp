#include "ShadowMapManager.h"

#include <limits>

#include "Renderer.h"
#include <glm/gtc/matrix_transform.hpp>

namespace ailo {
    void ShadowMapManager::update(const ViewInfo& camera, const glm::vec3& lightDirection, const ShadowSettings& settings, uint32_t tileSize) {
        m_cascadeCount = std::clamp<uint32_t>(settings.cascadeCount, 1, kMaxShadowCascades);

        const float cameraNear = camera.nearPlane;
        const float cameraFar = camera.farPlane;
        const float shadowFar = std::clamp(settings.distance, cameraNear * 2.0f, cameraFar);
        const auto splits = computeSplits(static_cast<uint8_t>(m_cascadeCount), cameraNear, shadowFar, settings.splitLambda);

        const glm::mat4 viewFromClip = glm::inverse(camera.projection);
        constexpr std::array<glm::vec2, 4> kNdcCorners { glm::vec2 { -1, -1 }, { 1, -1 }, { -1, 1 }, { 1, 1 } };
        std::array<glm::vec3, 4> nearCorners;
        std::array<glm::vec3, 4> farCorners;
        for (size_t c = 0; c < kNdcCorners.size(); c++) {
            const glm::vec4 n = viewFromClip * glm::vec4(kNdcCorners[c], 0.0f, 1.0f);
            const glm::vec4 f = viewFromClip * glm::vec4(kNdcCorners[c], 1.0f, 1.0f);
            nearCorners[c] = glm::vec3(n) / n.w;
            farCorners[c] = glm::vec3(f) / f.w;
        }
        const glm::mat4 worldFromView = glm::inverse(camera.view);

        const glm::vec3 dir = glm::normalize(lightDirection);
        // lookAt degenerates when the light direction is parallel to up.
        const glm::vec3 up = std::abs(dir.y) > 0.99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
        const glm::mat4 lightRotation = glm::lookAt(glm::vec3(0.0f), dir, up);

        for (uint32_t i = 0; i < m_cascadeCount; i++) {
            ShadowCascade& cascade = m_cascades[i];
            cascade.splitFar = splits[i + 1];

            // Frustum edges are straight lines in view depth (perspective or orthographic), so slice corners are lerps
            // between the near and far corners.
            const float tNear = (splits[i] - cameraNear) / (cameraFar - cameraNear);
            const float tFar = (splits[i + 1] - cameraNear) / (cameraFar - cameraNear);
            std::array<glm::vec3, 8> corners;
            for (size_t c = 0; c < 4; c++) {
                corners[c] = glm::mix(nearCorners[c], farCorners[c], tNear);
                corners[c + 4] = glm::mix(nearCorners[c], farCorners[c], tFar);
            }

            glm::vec3 viewCenter { 0.0f };
            for (const glm::vec3& corner : corners) {
                viewCenter += corner;
            }
            viewCenter /= static_cast<float>(corners.size());
            float radius = 0.0f;
            for (const glm::vec3& corner : corners) {
                radius = std::max(radius, glm::distance(viewCenter, corner));
            }
            const glm::vec3 center = glm::vec3(worldFromView * glm::vec4(viewCenter, 1.0f));

            // Moving the light only by whole texels keeps static shadow edges from shimmering as the camera moves.
            glm::vec3 lightSpaceCenter = glm::vec3(lightRotation * glm::vec4(center, 1.0f));
            const float texelSize = 2.0f * radius / static_cast<float>(tileSize);
            lightSpaceCenter.x = std::floor(lightSpaceCenter.x / texelSize) * texelSize;
            lightSpaceCenter.y = std::floor(lightSpaceCenter.y / texelSize) * texelSize;

            // The light looks down its -Z, so the eye sits on +Z, behind the sphere.
            const float backDistance = radius + settings.casterDistance;
            const glm::vec3 eye = lightSpaceCenter + glm::vec3(0.0f, 0.0f, backDistance);
            cascade.view = glm::translate(glm::mat4(1.0f), -eye) * lightRotation;
            cascade.projection = glm::ortho(-radius, radius, -radius, radius, 0.0f, backDistance + radius);
            cascade.projection[1][1] *= -1.0f;
            cascade.viewProjection = cascade.projection * cascade.view;
        }
    }

}
