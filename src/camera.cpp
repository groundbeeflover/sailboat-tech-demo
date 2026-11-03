#include "camera.h"
#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <glm/gtc/matrix_transform.hpp>
DISABLE_WARNINGS_POP()
#include <algorithm>
#include <cmath>

CameraView Cameras::update(CameraMode mode, const CameraInputs& in)
{
    CameraView cam;
    const glm::mat4 perspective = glm::perspective(glm::radians(FieldOfViewDeg), in.aspect, NearPlane, FarPlane);

    switch (mode) {
    case CameraMode::Chase: {
        // Third-person camera behind the boat. It follows the heading only (not pitch/roll) and
        // eases towards its target, which feels like a camera boat following the yacht.
        const glm::vec3 forward { std::sin(in.boatYaw), 0.0f, std::cos(in.boatYaw) };
        const glm::vec3 target = in.boatPosition - forward * 17.0f + glm::vec3(0.0f, 5.5f, 0.0f);
        if (!m_chaseInitialised) {
            m_chasePosition = target;
            m_chaseInitialised = true;
        }
        // Frame-rate independent exponential smoothing.
        const float alpha = 1.0f - std::exp(-in.dt * 2.5f);
        m_chasePosition += (target - m_chasePosition) * alpha;
        const glm::vec3 lookAt = in.boatPosition + glm::vec3(0.0f, 3.0f, 0.0f);
        cam.view = glm::lookAt(m_chasePosition, lookAt, glm::vec3(0, 1, 0));
        cam.projection = perspective;
        cam.position = m_chasePosition;
        cam.forward = glm::normalize(lookAt - m_chasePosition);
    } break;

    case CameraMode::Deck: {
        // The camera is a node in the boat hierarchy, so it inherits heave, pitch and roll.
        // A camera's view matrix is simply the inverse of its world matrix.
        cam.view = glm::inverse(in.deckCameraWorld);
        cam.projection = perspective;
        cam.position = glm::vec3(in.deckCameraWorld[3]);
        cam.forward = -glm::normalize(glm::vec3(in.deckCameraWorld[2]));
    } break;

    case CameraMode::Top: {
        // Orthographic map view of the whole course, +x to the right and -z up on screen.
        const float halfHeight = in.courseRadius * 1.25f;
        const float halfWidth = halfHeight * in.aspect;
        const glm::vec3 eye = in.courseCenter + glm::vec3(0.0f, 500.0f, 0.0f);
        cam.view = glm::lookAt(eye, in.courseCenter, glm::vec3(0, 0, -1));
        cam.projection = glm::ortho(-halfWidth, halfWidth, -halfHeight, halfHeight, 1.0f, 1200.0f);
        cam.position = eye;
        cam.forward = glm::vec3(0, -1, 0);
        cam.orthographic = true;
    } break;

    case CameraMode::Orbit: {
        const glm::vec3 target = in.boatPosition + glm::vec3(0.0f, 2.0f, 0.0f);
        const glm::vec3 offset = m_orbitDistance * glm::vec3(std::cos(m_orbitPitch) * std::sin(m_orbitYaw), std::sin(m_orbitPitch), std::cos(m_orbitPitch) * std::cos(m_orbitYaw));
        cam.position = target + offset;
        cam.view = glm::lookAt(cam.position, target, glm::vec3(0, 1, 0));
        cam.projection = perspective;
        cam.forward = -glm::normalize(offset);
    } break;
    }
    return cam;
}

void Cameras::onMouseMove(const glm::vec2& cursor, bool dragging)
{
    if (dragging && m_wasDragging) {
        const glm::vec2 delta = cursor - m_lastCursor;
        m_orbitYaw -= delta.x * 0.006f;
        m_orbitPitch = std::clamp(m_orbitPitch + delta.y * 0.006f, -0.05f, 1.5f);
    }
    m_lastCursor = cursor;
    m_wasDragging = dragging;
}

void Cameras::onScroll(float delta)
{
    m_orbitDistance = std::clamp(m_orbitDistance * std::pow(0.9f, delta), 4.0f, 150.0f);
}
