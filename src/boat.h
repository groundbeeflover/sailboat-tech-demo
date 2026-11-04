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
#include <span>
#include <memory>
#include <vector>

class BezierPath;
class Waves;


class Boat {
public:
    Boat();

    // ---- Free sailing (A5): steered by hand instead of following the course.
    // A round obstacle on the water (a buoy, the island) that the boat bounces off.
    struct Obstacle {
        glm::vec2 center { 0.0f }; // world xz
        float radius { 0.0f };
    };
    // What the boat needs from outside while it is free sailing.
    struct Helm {
        float steer { 0.0f }; // -1 = hard to port (A, left arrow), +1 = hard to starboard (D, right arrow)
        std::span<const Obstacle> obstacles;
    };
    // The hardest hit of this frame; hit = false when the boat touched nothing.
    struct Contact {
        bool hit { false };
        size_t obstacle { 0 }; // index into Helm::obstacles
        glm::vec3 point { 0.0f }; // on the obstacle's edge, at the water surface
        glm::vec2 normal { 0.0f }; // world xz, from the obstacle towards the boat
        float speed { 0.0f }; // m/s with which the boat ran into it
    };

    void update(float dt, float time, const FeatureToggles& toggles, const BezierPath& course, const Waves& waves, const Helm& helm);
    const Contact& contact() const { return m_contact; }

    SceneNode& root() { return *m_root; }
    glm::vec3 coursePosition() const { return m_position; } // on the course, y = 0
    float yaw() const { return m_yaw; }
    const glm::mat4& deckCameraWorld() const { return m_deckCamera->world; }
    // Spray emitters: the stem and the transom on the waterline (axes: +x port, +y up, +z forward).
    const glm::mat4& bowWorld() const { return m_bow->world; }
    const glm::mat4& sternWorld() const { return m_stern->world; }


    // Values shown in the UI (and handy for report screenshots).
    struct Telemetry {
        float speed { 0.0f }; // measured from the actual motion, m/s
        float courseParameter { 0.0f }; // Bezier parameter u
        float distance { 0.0f }; // arc length along the course
        float windAngleDeg { 0.0f }; // 0 = head to wind, 180 = dead downwind
        float boomDeg { 0.0f };
        float heelDeg { 0.0f };
        float rudderDeg { 0.0f };
        float sailWeight { 0.0f }; // blend-shape weight of the sail: 0 = flat, +-1 = full to boom-local +-x
    };
    const Telemetry& telemetry() const { return m_telemetry; }

private:
    GPUMesh* addMesh(const Mesh& mesh);
    Texture* addTexture(const ImageRGBA& image, bool srgb);
    void collide(glm::vec2& xz, float yaw, const Helm& helm, const Waves& waves, float time);

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
    SceneNode* m_bow { nullptr };
    SceneNode* m_stern { nullptr };


    // Motion state
    bool m_initialised { false };
    float m_u { 0.0f };
    float m_distance { 0.0f };
    glm::vec3 m_position { 0.0f };
    float m_yaw { 0.0f };
    float m_windAngle { 1.5f };
    glm::vec2 m_velocity { 0.0f }; // free sailing: world xz, m/s
    bool m_wasFreeSailing { false };
    Contact m_contact;


    // Smoothed pose (gives the boat some inertia)
    float m_heave { 0.0f }, m_pitch { 0.0f }, m_roll { 0.0f }, m_heel { 0.0f };
    float m_boomAngle { 0.0f }, m_billow { 0.5f }, m_rudderAngle { 0.0f };

    Telemetry m_telemetry;
};
