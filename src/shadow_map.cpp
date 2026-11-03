#include "shadow_map.h"
#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
DISABLE_WARNINGS_POP()
#include <cmath>
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
