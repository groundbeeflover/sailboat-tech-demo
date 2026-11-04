#include "minimap.h"
#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <glm/gtc/matrix_transform.hpp>
DISABLE_WARNINGS_POP()
#include <algorithm>
#include <cmath>
#include <iostream>

Minimap::~Minimap()
{
    if (m_fbo)
        glDeleteFramebuffers(1, &m_fbo);
    if (m_color)
        glDeleteTextures(1, &m_color);
}

void Minimap::resize(int requestedSide)
{
    const int side = std::clamp(requestedSide, 128, 512);
    if (side == m_side)
        return;

    m_hdr.resize({ side, side });
    // Allocate GL names once. Reallocate the RGBA8 storage *in place* to keep ImGui's recorded
    // ImTextureID valid even when the user changes the resolution in the current UI frame.
    if (!m_color) {
        glGenTextures(1, &m_color);
        glBindTexture(GL_TEXTURE_2D, m_color);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    } else {
        glBindTexture(GL_TEXTURE_2D, m_color);
    }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, side, side, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    if (!m_fbo)
        glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_color, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cerr << "Minimap framebuffer is incomplete\n";
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    m_side = side;
}

void Minimap::bindLdr() const
{
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, m_side, m_side);
}

CameraView Minimap::camera(const MinimapInputs& inputs, bool headingUp)
{
    CameraView view;
    const glm::vec3 center = inputs.courseCenter;
    // Fix the projection around the course rather than the moving boat: both the path and island
    // stay visible, and the player can judge progress over an entire lap.
    const float halfExtent = std::max(10.0f, inputs.courseRadius * 1.25f);
    view.position = center + glm::vec3(0.0f, 500.0f, 0.0f);
    // World -Z points up in the existing top camera. In heading-up mode the +forward axis of the
    // boat points up instead; changing this vector rotates the map without changing its extent.
    const glm::vec3 mapUp = headingUp
        ? glm::vec3(std::sin(inputs.boatYaw), 0.0f, std::cos(inputs.boatYaw))
        : glm::vec3(0.0f, 0.0f, -1.0f);
    view.view = glm::lookAt(view.position, center, mapUp);
    view.projection = glm::ortho(-halfExtent, halfExtent, -halfExtent, halfExtent, 1.0f, 1200.0f);
    view.forward = { 0.0f, -1.0f, 0.0f };
    view.orthographic = true; // disables fog and skybox like the existing top-down camera
    return view;
}
