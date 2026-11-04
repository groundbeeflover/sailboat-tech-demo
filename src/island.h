#pragma once
#include "draw_item.h"
#include "feature_toggles.h"
#include "lights.h"
#include "mesh.h"
#include "procedural_texture.h"
#include "scene_node.h"
#include "texture.h"
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
//   ├─ Terrain (static fBm height field, sand/grass/rock maps baked from its height and slope)
//   └─ Lighthouse (rest: on the levelled plateau at the top)
//      └─ Tower (+ Bands)
//         └─ Gallery
//            └─ LampHouse
//               └─ Lamp (anim: Ry(angle), the lamp turns; emissive at night)
//
// The island itself never moves, and the waves and the camera need nothing from it: the water is
// drawn after the opaque terrain with depth testing, and view-dependent shading comes from the
// per-frame uniforms the renderer sets for every DrawItem. update() only animates the lighthouse.
//
// The constructor needs an OpenGL context (it uploads meshes and textures); update() is pure math.
class Island {
public:
    explicit Island(const glm::vec3& position);

    // Turns the lamp, switches it on at dusk and off at dawn, and recomputes the world matrices.
    void update(float dt, const FeatureToggles& toggles, const Lighting& lighting);

    SceneNode& root() { return *m_root; }
    // The lamp as a spotlight for the renderer, from the Lamp node's world matrix (valid after update()).
    SpotLight lampLight() const;
    bool lampOn() const { return m_root->visible && m_night > 0.0f; }

    glm::vec3 position() const { return m_position; } // centre, on the waterline
    // Radius of the circle around position() that holds all the dry land (for clearance checks).
    float shoreRadius() const { return m_shoreRadius; }

private:
    GPUMesh* addMesh(const Mesh& mesh);
    Texture* addTexture(const ImageRGBA& image, bool srgb);

    std::vector<std::unique_ptr<GPUMesh>> m_meshes;
    std::vector<std::unique_ptr<Texture>> m_textures;
    PBRMaterial m_terrainMaterial, m_whiteMaterial, m_redMaterial, m_metalMaterial, m_lampMaterial;

    std::unique_ptr<SceneNode> m_root;
    SceneNode* m_lamp { nullptr };

    glm::vec3 m_position;
    float m_shoreRadius { 0.0f };
    float m_beamTilt { 0.0f }; // radians below the horizon, set so the beam reaches the course
    float m_lampAngle { 0.0f }; // radians about +y, integrated from the speed so slider changes don't jump
    float m_night { 0.0f }; // 0 = day (lamp off) .. 1 = night (lamp fully on)
};
