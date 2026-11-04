#include "course.h"
#include "procedural_mesh.h"
#include "waves.h"
#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
DISABLE_WARNINGS_POP()
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>



Course::Course()
{
    // Five anchors give a closed path of five cubic segments (the assignment asks for at least three).
    m_anchors = {
        { 0.0f, 0.0f, -120.0f },
        { 110.0f, 0.0f, -60.0f },
        { 120.0f, 0.0f, 70.0f },
        { -10.0f, 0.0f, 120.0f },
        { -120.0f, 0.0f, 20.0f },
    };
    for (const glm::vec3& a : m_anchors)
        m_center += a / static_cast<float>(m_anchors.size());
    for (const glm::vec3& a : m_anchors)
        m_radius = std::max(m_radius, glm::length(a - m_center) + 20.0f);

    // Buoy: tapered float with a ball on top.
    Mesh body = makeCylinder(BuoyRadius, 0.45f, 1.3f);
    transformMesh(body, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.5f, 0.0f)));
    Mesh ball = makeSphere(0.45f);
    transformMesh(ball, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.85f, 0.0f)));
    const std::array<Mesh, 2> parts { body, ball };
    m_buoyMesh = std::make_unique<GPUMesh>(mergeMeshList(parts));
    m_buoyMaterial = { .albedo = glm::vec3(0.95f, 0.33f, 0.03f), .roughness = 0.35f };

    m_root = std::make_unique<SceneNode>("Course");
    for (const glm::vec3& anchor : m_anchors) {
        // Inside each turn, so the boat rounds the buoy instead of hitting it.
        m_buoyPositions.push_back(anchor + glm::normalize(m_center - anchor) * 9.0f);
        m_buoys.push_back(&m_root->addChild("Buoy", m_buoyMesh.get(), &m_buoyMaterial));
    }
    m_knocks.resize(m_buoys.size());
    setTension(1.0f);
}

void Course::setTension(float tension)
{
    if (tension == m_tension)
        return;
    m_tension = tension;
    m_path = BezierPath::closedLoop(m_anchors, tension);
}

float Course::distanceTo(const glm::vec3& point) const
{
    const glm::vec2 p(point.x, point.z);
    float nearest = std::numeric_limits<float>::max();
    for (const glm::vec3& q : m_path.sample(64)) // ~2 m apart, plenty for a clearance check
        nearest = std::min(nearest, glm::distance(p, glm::vec2(q.x, q.z)));
    for (const glm::vec3& b : m_buoyPositions)
        nearest = std::min(nearest, glm::distance(p, glm::vec2(b.x, b.z)) - BuoyRadius);
    return nearest;
}

void Course::update(float time, const Waves& waves)
{
    for (size_t i = 0; i < m_buoys.size(); ++i) {
        const glm::vec2 xz(m_buoyPositions[i].x, m_buoyPositions[i].z);
        const float height = waves.heightAt(xz, time);
        // Tilt with the local surface normal (approximated at the rest point).
        const glm::vec3 n = waves.normal(xz, time);
        const float tiltX = std::atan2(n.z, n.y);
        const float tiltZ = -std::atan2(n.x, n.y);
        // Hit by the boat (A5): tip away from it and rock back. This is the impulse response of a
        // damped spring, theta(t) = A e^(-t / tau) sin(omega t), evaluated in closed form, so it looks
        // the same at any frame rate. It turns about the horizontal axis at right angles to the push.
        glm::mat4 rocking(1.0f);
        const Knock& k = m_knocks[i];
        const float sinceHit = time - k.time;
        if (k.amplitude > 0.0f && sinceHit >= 0.0f) {
            const float rock = k.amplitude * std::exp(-sinceHit / 1.2f) * std::sin(4.2f * sinceHit);
            rocking = glm::rotate(glm::mat4(1.0f), rock, glm::vec3(k.direction.y, 0.0f, -k.direction.x));
        }
        m_buoys[i]->anim = glm::translate(glm::mat4(1.0f), glm::vec3(xz.x, height, xz.y)) * rocking
            * glm::rotate(glm::mat4(1.0f), tiltZ, glm::vec3(0, 0, 1))
            * glm::rotate(glm::mat4(1.0f), tiltX, glm::vec3(1, 0, 0));
    }
    m_root->updateWorld();
}

void Course::knock(size_t buoy, const glm::vec2& direction, float speed, float time)
{
    if (buoy >= m_knocks.size() || glm::length(direction) < 1e-4f)
        return;
    // A harder hit tips it further, up to about 30 degrees.
    m_knocks[buoy] = { .time = time, .direction = glm::normalize(direction), .amplitude = std::min(0.15f * speed, 0.5f) };
}



void Course::appendLines(std::vector<LineBatch>& out, const FeatureToggles& toggles, const Waves& waves, float time) const
{
    // Lines are draped over the waves so they stay visible from low viewpoints.
    auto onWater = [&](glm::vec3 p) {
        p.y = waves.heightAt(glm::vec2(p.x, p.z), time) + 0.4f;
        return p;
    };

    if (toggles.showCurve) {
        LineBatch curve { .mode = LineMode::Loop, .color = glm::vec3(2.0f, 1.3f, 0.15f) };
        for (const glm::vec3& p : m_path.sample(120))
            curve.points.push_back(onWater(p));
        out.push_back(std::move(curve));
    }

    if (toggles.showControlPolygon) {
        // Handles: each anchor connected to its neighbouring inner control points.
        LineBatch handles { .mode = LineMode::Segments, .color = glm::vec3(0.2f, 1.2f, 1.6f) };
        LineBatch anchors { .mode = LineMode::Points, .color = glm::vec3(2.0f, 1.3f, 0.15f), .pointSize = 10.0f };
        LineBatch inner { .mode = LineMode::Points, .color = glm::vec3(0.2f, 1.2f, 1.6f), .pointSize = 7.0f };
        for (size_t i = 0; i < m_path.segmentCount(); ++i) {
            const CubicBezier& s = m_path.segment(i);
            for (const glm::vec3& p : { s.p0, s.p1, s.p2, s.p3 })
                handles.points.push_back(onWater(p));
            anchors.points.push_back(onWater(s.p0));
            inner.points.push_back(onWater(s.p1));
            inner.points.push_back(onWater(s.p2));
        }
        out.push_back(std::move(handles));
        out.push_back(std::move(anchors));
        out.push_back(std::move(inner));
    }
}
