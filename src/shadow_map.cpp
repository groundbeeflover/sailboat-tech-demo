#include "shadow_map.h"
#include "lights.h"
#include "camera.h"
#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
DISABLE_WARNINGS_POP()
#include <cmath>
#include <algorithm>
#include <limits>
#include <iostream>

ShadowMap::~ShadowMap()
{
    release();
}

void ShadowMap::release()
{
    if (m_depth)
        glDeleteTextures(1, &m_depth);
    if (m_fbo)
        glDeleteFramebuffers(1, &m_fbo);
    m_fbo = m_depth = 0;
}

void ShadowMap::resize(int resolution)
{
    if (resolution == m_resolution)
        return;
    release();
    m_resolution = resolution;

    glGenTextures(1, &m_depth);
    glBindTexture(GL_TEXTURE_2D, m_depth);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, resolution, resolution, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    // Linear filtering + compare mode = 2x2 percentage-closer filtering in hardware for every lookup.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    // Outside the map counts as "nothing in front": depth 1 is never closer than the receiver.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    const float border[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);

    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_depth, 0);
    glDrawBuffer(GL_NONE); // depth only
    glReadBuffer(GL_NONE);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cerr << "Shadow map framebuffer is incomplete" << std::endl;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void ShadowMap::bind() const
{
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, m_resolution, m_resolution);
}

glm::mat4 directionalLightMatrix(const glm::vec3& lightDirection, const glm::vec3& center, float halfExtent, float depthRange, int resolution)
{
    // Rotation-only view that looks along the light's travel direction (from the light towards the scene).
    const glm::vec3 towardsLight = glm::normalize(lightDirection);
    const glm::vec3 up = std::abs(towardsLight.y) > 0.99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
    const glm::mat4 view = glm::lookAt(glm::vec3(0.0f), -towardsLight, up);

    // Centre of the box in light space, snapped to the texel grid.
    glm::vec3 c = glm::vec3(view * glm::vec4(center, 1.0f));
    const float texel = 2.0f * halfExtent / static_cast<float>(resolution);
    c.x = std::floor(c.x / texel) * texel;
    c.y = std::floor(c.y / texel) * texel;

    // The view looks down -z, so the near/far distances are measured along -z.
    const glm::mat4 projection = glm::ortho(c.x - halfExtent, c.x + halfExtent, c.y - halfExtent, c.y + halfExtent, -c.z - depthRange, -c.z + depthRange);
    return projection * view;
}

glm::mat4 spotLightMatrix(const SpotLight& spot, float nearPlane)
{
    const glm::vec3 direction = glm::normalize(spot.direction);
    const glm::vec3 up = std::abs(direction.y) > 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    const glm::mat4 view = glm::lookAt(spot.position, spot.position + direction, up);
    // SpotLight's angles are half-angles; glm::perspective takes full vertical FOV.
    return glm::perspective(2.0f * spot.outerAngle, 1.0f, nearPlane, spot.range) * view;
}

glm::mat4 directionalCascadeMatrix(const CameraView& camera, const glm::vec3& lightDirection,
    float nearDepth, float farDepth, int resolution)
{
    // Unproject the camera's eight frustum corners, then interpolate to the chosen depth slice.
    // Perspective rays are linear in eye-space depth, even though hardware depth itself is not.
    const glm::mat4 inverseVP = glm::inverse(camera.projection * camera.view);
    const float t0 = std::clamp((nearDepth - Cameras::NearPlane) / (Cameras::FarPlane - Cameras::NearPlane), 0.0f, 1.0f);
    const float t1 = std::clamp((farDepth - Cameras::NearPlane) / (Cameras::FarPlane - Cameras::NearPlane), 0.0f, 1.0f);
    const glm::vec3 towards = glm::normalize(lightDirection);
    const glm::vec3 up = std::abs(towards.y) > 0.99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
    const glm::mat4 lightView = glm::lookAt(glm::vec3(0), -towards, up);
    glm::vec3 minBounds(std::numeric_limits<float>::max());
    glm::vec3 maxBounds(std::numeric_limits<float>::lowest());
    for (int y : { -1, 1 }) {
        for (int x : { -1, 1 }) {
            glm::vec4 a = inverseVP * glm::vec4(static_cast<float>(x), static_cast<float>(y), -1.0f, 1.0f);
            glm::vec4 b = inverseVP * glm::vec4(static_cast<float>(x), static_cast<float>(y), 1.0f, 1.0f);
            const glm::vec3 start = glm::vec3(a) / a.w;
            const glm::vec3 end = glm::vec3(b) / b.w;
            for (float t : { t0, t1 }) {
                const glm::vec3 lightPoint = glm::vec3(lightView * glm::vec4(glm::mix(start, end, t), 1.0f));
                minBounds = glm::min(minBounds, lightPoint);
                maxBounds = glm::max(maxBounds, lightPoint);
            }
        }
    }
    const glm::vec3 midpoint = (minBounds + maxBounds) * 0.5f;
    // Equal x/y extents keep light texels square. A texel-snapped centre limits shimmer.
    const float extent = std::max((maxBounds.x - minBounds.x), (maxBounds.y - minBounds.y)) * 0.55f + 1.0f;
    const float texel = 2.0f * extent / static_cast<float>(resolution);
    const float cx = std::floor(midpoint.x / texel) * texel;
    const float cy = std::floor(midpoint.y / texel) * texel;
    // Include casters outside the visible slice that cast *into* it, notably the tall mast.
    constexpr float casterPadding = 55.0f;
    return glm::ortho(cx - extent, cx + extent, cy - extent, cy + extent,
        -maxBounds.z - casterPadding, -minBounds.z + casterPadding) * lightView;
}
