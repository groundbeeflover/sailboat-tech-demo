#pragma once
#include "bezier.h"
#include "draw_item.h"
#include "feature_toggles.h"
#include "mesh.h"
#include "scene_node.h"
#include <memory>
#include <vector>

class Waves;

// The race course: a closed Bezier path through the anchor points, with a buoy inside each turn.
// Also produces the debug lines for the curve and its control polygon.
class Course {
public:
    Course(); // needs an OpenGL context (buoy mesh)

    // Rebuilds the path when the tension slider changes.
    void setTension(float tension);
    // Buoys bob on the waves.
    void update(float time, const Waves& waves);

    const BezierPath& path() const { return m_path; }
    SceneNode& root() { return *m_root; }
    glm::vec3 center() const { return m_center; }
    float radius() const { return m_radius; }
    // Smallest horizontal distance from a point to the path or the edge of a buoy, e.g. to check
    // that something placed inside the loop stays clear of the course.
    float distanceTo(const glm::vec3& point) const;
    // Free sailing (A5): the buoys are obstacles the boat can hit.
    static constexpr float BuoyRadius = 0.7f; // widest point of the float
    const std::vector<glm::vec3>& buoyPositions() const { return m_buoyPositions; } // centres, y = 0
    // The boat ran into buoy `buoy`, pushing it along `direction` (world xz) at `speed` m/s: it tips
    // away and rocks back over the next seconds (see update()).
    void knock(size_t buoy, const glm::vec2& direction, float speed, float time);


    void appendLines(std::vector<LineBatch>& out, const FeatureToggles& toggles, const Waves& waves, float time) const;

private:
    std::vector<glm::vec3> m_anchors;
    BezierPath m_path;
    float m_tension { -1.0f };
    glm::vec3 m_center { 0.0f };
    float m_radius { 0.0f };

    std::unique_ptr<GPUMesh> m_buoyMesh;
    PBRMaterial m_buoyMaterial;
    std::unique_ptr<SceneNode> m_root;
    std::vector<SceneNode*> m_buoys;
    std::vector<glm::vec3> m_buoyPositions;
    // The last hit of each buoy, for the rocking in update().
    struct Knock {
        float time { 0.0f }; // when it was hit
        glm::vec2 direction { 1.0f, 0.0f }; // world xz, the way it was pushed
        float amplitude { 0.0f }; // radians; 0 = never hit
    };
    std::vector<Knock> m_knocks;

};
