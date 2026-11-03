#pragma once
#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <glm/vec3.hpp>
DISABLE_WARNINGS_POP()

// Light sources shared between the scene and the renderer. The scene side fills them in every
// frame (e.g. from a node's world matrix), the renderer only reads them via FrameContext.
// The sun/moon is described by `Lighting` in sky.h.

// A cone light, e.g. the lighthouse lamp. Disabled by default, so the renderer can skip it.
struct SpotLight {
    bool enabled { false };
    glm::vec3 position { 0.0f };
    glm::vec3 direction { 0.0f, -1.0f, 0.0f }; // unit vector, where the cone points
    glm::vec3 color { 0.0f }; // HDR intensity, can be much larger than 1
    float innerAngle { 0.1396f }; // radians (8 deg): full intensity inside this half-angle
    float outerAngle { 0.2094f }; // radians (12 deg): no light outside this half-angle
    float range { 300.0f }; // metres, also the far plane of its shadow map
};
