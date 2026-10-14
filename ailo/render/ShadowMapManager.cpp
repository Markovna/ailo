#include "ShadowMapManager.h"

#include "Renderer.h"
#include <glm/gtc/matrix_transform.hpp>

#include "ecs/SceneLighting.h"

namespace ailo {
    void ShadowMapManager::updateCascadeShadows(const ViewInfo& camera, const SceneLighting* sceneLighting) {
        constexpr uint32_t cascadeCount = 4;
        m_cascadeShadows.resize(cascadeCount);

        glm::mat4 lightViewAtOrigin = glm::lookAt(glm::vec3 {}, sceneLighting->lightDirection, glm::vec3 { 0.0, 1.0, 0.0 });
        auto cameraProjection = camera.projection;

        auto cascadeSplits = computeSplits(cascadeCount, camera.nearPlane, camera.farPlane);
        for (uint32_t i = 0; i < m_cascadeShadows.size(); i++) {
            ShadowMap& shadow = m_cascadeShadows[i];

            updateProjection(&cameraProjection, cascadeSplits[i], cascadeSplits[i+1]);


        }
    }

    void ShadowMapManager::updateProjection(glm::mat4* projection, float nearPlane, float farPlane) {
        auto& p = *projection;
        glm::vec4 const N = { 0, 0,  1,  nearPlane };  // near plane equation
        glm::vec4 const F = { 0, 0, -1, -farPlane };  // far plane equation
        // near plane equation scale factor
        float const sn = -2.0f * (p[2].w * F.w - p[3].w * F.z) / (N.z * F.w - N.w * F.z);
        // far plane equation scale factor
        float const sf = -2.0f * (p[2].w * N.w - p[3].w * N.z) / (F.z * N.w - F.w * N.z);
        // New values for the projection
        p[2].z = (sf * F.z - sn * N.z) * 0.5f;
        p[3].z = (sf * F.w - sn * N.w) * 0.5f;
    }
}
