#pragma once
#include "draw_item.h"
#include "feature_toggles.h"
#include "lights.h"
#include "mesh.h"
#include "scene_node.h"
#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <glm/vec3.hpp>
DISABLE_WARNINGS_POP()
#include <memory>
#include <vector>

struct Lighting;

// A rocky island with a lighthouse, inside the course.
//
//   Island (rest: position in the world)
//   ├─ Terrain (static; placeholder dome until the fbm heightfield replaces it)
//   └─ Lighthouse (rest: on top of the terrain)
//      └─ Tower (+ Bands)
//         └─ Gallery
//            └─ LampHouse
//               └─ Lamp (anim: Ry(angle), the lamp turns; emissive at night)
//
// The island itself never moves, and the waves and the camera need nothing from it: the water is
// drawn after the opaque terrain with depth testing, and view-dependent shading comes from the
// per-frame uniforms the renderer sets for every DrawItem. update() only animates the lighthouse.
//
// The constructor needs an OpenGL context (it uploads meshes); update() is pure math.
class Island {
public:
    explicit Island(const glm::vec3& position);

    // Turns the lamp, switches it on at dusk and off at dawn, and recomputes the world matrices.
    void update(float dt, const FeatureToggles& toggles, const Lighting& lighting);

    SceneNode& root() { return *m_root; }
    // The lamp as a spotlight for the renderer, from the Lamp node's world matrix (valid after update()).
    SpotLight lampLight() const;
    bool lampOn() const { return m_root->visible && m_night > 0.0f; }

private:
    GPUMesh* addMesh(const Mesh& mesh);

    std::vector<std::unique_ptr<GPUMesh>> m_meshes;
    PBRMaterial m_rockMaterial, m_whiteMaterial, m_redMaterial, m_metalMaterial, m_lampMaterial;

    std::unique_ptr<SceneNode> m_root;
    SceneNode* m_lamp { nullptr };

    float m_lampAngle { 0.0f }; // radians about +y, integrated from the speed so slider changes don't jump
    float m_night { 0.0f }; // 0 = day (lamp off) .. 1 = night (lamp fully on)
};
