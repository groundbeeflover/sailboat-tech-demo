#pragma once
#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <glm/vec3.hpp>
DISABLE_WARNINGS_POP()
#include <vector>

struct CubicBezier {
    glm::vec3 p0, p1, p2, p3;

    // B(t) = (1-t)^3 p0 + 3(1-t)^2 t p1 + 3(1-t) t^2 p2 + t^3 p3
    glm::vec3 evaluate(float t) const;
    // B'(t) = 3(1-t)^2 (p1-p0) + 6(1-t)t (p2-p1) + 3t^2 (p3-p2)
    glm::vec3 derivative(float t) const;
};

// A closed path of cubic Bezier segments. The global parameter u runs from 0 to segmentCount():
// the integer part picks the segment, the fraction is that segment's t.
class BezierPath {
public:
    // Closed loop through the anchors with C1 joins. The inner control points follow the
    // Catmull-Rom tangent at each anchor:
    //     p1 = P_i     + tension * (P_{i+1} - P_{i-1}) / 6
    //     p2 = P_{i+1} - tension * (P_{i+2} - P_i)   / 6
    // so the control points on both sides of an anchor are mirror images of each other through it.
    static BezierPath closedLoop(const std::vector<glm::vec3>& anchors, float tension);

    size_t segmentCount() const { return m_segments.size(); }
    const CubicBezier& segment(size_t i) const { return m_segments[i]; }

    glm::vec3 evaluate(float u) const;
    glm::vec3 tangent(float u) const; // not normalised

    // Arc length. The table maps sampled u values to the distance travelled, so a distance can be
    // turned back into a parameter by binary search + linear interpolation (constant-speed motion).
    float length() const { return m_tableDistance.empty() ? 0.0f : m_tableDistance.back(); }
    float distanceToParameter(float distance) const; // wraps around the loop
    float parameterToDistance(float u) const;

    // Evenly spaced in u, for drawing.
    std::vector<glm::vec3> sample(int samplesPerSegment) const;

private:
    void buildArcLengthTable(int samplesPerSegment);
    // Splits u into a segment index and a local t in [0, 1].
    const CubicBezier& locate(float u, float& t) const;

    std::vector<CubicBezier> m_segments;
    std::vector<float> m_tableU;
    std::vector<float> m_tableDistance;
};
