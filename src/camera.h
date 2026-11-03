#pragma once
#include "feature_toggles.h"
#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
DISABLE_WARNINGS_POP()

// Everything the renderer needs to know about the active viewpoint.
struct CameraView {
    glm::mat4 view { 1.0f };
    glm::mat4 projection { 1.0f };
    glm::vec3 position { 0.0f };
    glm::vec3 forward { 0.0f, 0.0f, -1.0f };
    bool orthographic { false };
};

// What the cameras follow. Plain data, so the camera code does not depend on the Boat class.
struct CameraInputs {
    glm::vec3 boatPosition { 0.0f }; // on the course, without wave heave (keeps the chase camera calm)
    float boatYaw { 0.0f };
    glm::mat4 deckCameraWorld { 1.0f }; // world matrix of the "DeckCamera" node in the boat hierarchy
    glm::vec3 courseCenter { 0.0f };
    float courseRadius { 100.0f };
    float aspect { 1.0f };
    float dt { 0.0f };
};

class Cameras {
public:
    CameraView update(CameraMode mode, const CameraInputs& inputs);

    // Orbit camera controls; the caller filters out input that ImGui wants.
    void onMouseMove(const glm::vec2& cursor, bool dragging);
    void onScroll(float delta);

    static constexpr float FieldOfViewDeg = 55.0f;
    static constexpr float NearPlane = 0.2f;
    static constexpr float FarPlane = 2000.0f;

private:
    glm::vec3 m_chasePosition { 0.0f };
    bool m_chaseInitialised { false };

    float m_orbitYaw { 2.4f };
    float m_orbitPitch { 0.35f };
    float m_orbitDistance { 20.0f };
    glm::vec2 m_lastCursor { 0.0f };
    bool m_wasDragging { false };
};
