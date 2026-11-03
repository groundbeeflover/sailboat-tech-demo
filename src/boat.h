#pragma once
#include "draw_item.h"
#include "feature_toggles.h"
#include "mesh.h"
#include "procedural_mesh.h"
#include "procedural_texture.h"
#include "scene_node.h"
#include "texture.h"
#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
DISABLE_WARNINGS_POP()
#include <memory>
#include <vector>

class BezierPath;
class Waves;

// The sailboat: its meshes, materials and transformation hierarchy.
//
//   Boat (anim: course position, heading, heave/pitch/roll from the waves, heel from the wind)
//   └─ Hull
//      ├─ Deck, Cabin, Rigging
//      ├─ Rudder (anim: turns with the course curvature)
//      │  └─ Tiller
//      ├─ Mast
//      │  ├─ Boom (anim: swings to leeward, rate-limited so tacks/gybes are visible)
//      │  │  └─ Sail (anim: non-uniform scale = billow depth and side)
//      │  └─ WindVane (anim: points into the wind)
//      └─ DeckCamera (no mesh; the deck viewpoint)
//
// The constructor needs an OpenGL context (it uploads meshes and textures); update() is pure math.
class Boat {
public:
    Boat();

    void update(float dt, float time, const FeatureToggles& toggles, const BezierPath& course, const Waves& waves);

    SceneNode& root() { return *m_root; }
    glm::vec3 coursePosition() const { return m_position; } // on the course, y = 0
    float yaw() const { return m_yaw; }
    const glm::mat4& deckCameraWorld() const { return m_deckCamera->world; }

    // Values shown in the UI (and handy for report screenshots).
    struct Telemetry {
        float speed { 0.0f }; // measured from the actual motion, m/s
        float courseParameter { 0.0f }; // Bezier parameter u
        float distance { 0.0f }; // arc length along the course
        float windAngleDeg { 0.0f }; // 0 = head to wind, 180 = dead downwind
        float boomDeg { 0.0f };
        float heelDeg { 0.0f };
        float rudderDeg { 0.0f };
    };
    const Telemetry& telemetry() const { return m_telemetry; }

private:
    GPUMesh* addMesh(const Mesh& mesh);
    Texture* addTexture(const ImageRGBA& image, bool srgb);

    HullShape m_shape;
    std::vector<std::unique_ptr<GPUMesh>> m_meshes;
    std::vector<std::unique_ptr<Texture>> m_textures;
    PBRMaterial m_hullMaterial, m_deckMaterial, m_cabinMaterial, m_aluminiumMaterial, m_steelMaterial,
        m_sailMaterial, m_varnishMaterial, m_vaneMaterial;

    std::unique_ptr<SceneNode> m_root;
    SceneNode* m_rudder { nullptr };
    SceneNode* m_boom { nullptr };
    SceneNode* m_sail { nullptr };
    SceneNode* m_vane { nullptr };
    SceneNode* m_deckCamera { nullptr };

    // Motion state
    bool m_initialised { false };
    float m_u { 0.0f };
    float m_distance { 0.0f };
    glm::vec3 m_position { 0.0f };
    float m_yaw { 0.0f };
    float m_windAngle { 1.5f };

    // Smoothed pose (gives the boat some inertia)
    float m_heave { 0.0f }, m_pitch { 0.0f }, m_roll { 0.0f }, m_heel { 0.0f };
    float m_boomAngle { 0.0f }, m_billow { 0.5f }, m_rudderAngle { 0.0f };

    Telemetry m_telemetry;
};
