#include "bezier.h"
#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <glm/geometric.hpp>
DISABLE_WARNINGS_POP()
#include <algorithm>
#include <cmath>

glm::vec3 CubicBezier::evaluate(float t) const
{
    const float s = 1.0f - t;
    return s * s * s * p0 + 3.0f * s * s * t * p1 + 3.0f * s * t * t * p2 + t * t * t * p3;
}

glm::vec3 CubicBezier::derivative(float t) const
{
    const float s = 1.0f - t;
    return 3.0f * s * s * (p1 - p0) + 6.0f * s * t * (p2 - p1) + 3.0f * t * t * (p3 - p2);
}

BezierPath BezierPath::closedLoop(const std::vector<glm::vec3>& anchors, float tension)
{
    BezierPath path;
    const size_t n = anchors.size();
    auto at = [&](size_t i) { return anchors[i % n]; };
    for (size_t i = 0; i < n; ++i) {
        const glm::vec3 previous = at(i + n - 1);
        const glm::vec3 start = at(i);
        const glm::vec3 end = at(i + 1);
        const glm::vec3 next = at(i + 2);
        CubicBezier segment;
        segment.p0 = start;
        segment.p1 = start + tension * (end - previous) / 6.0f;
        segment.p2 = end - tension * (next - start) / 6.0f;
        segment.p3 = end;
        path.m_segments.push_back(segment);
    }
    path.buildArcLengthTable(200);
    return path;
}

const CubicBezier& BezierPath::locate(float u, float& t) const
{
    const float count = static_cast<float>(m_segments.size());
    u = std::fmod(u, count);
    if (u < 0.0f)
        u += count;
    const size_t index = std::min(static_cast<size_t>(u), m_segments.size() - 1);
    t = u - static_cast<float>(index);
    return m_segments[index];
}

glm::vec3 BezierPath::evaluate(float u) const
{
    float t;
    const CubicBezier& segment = locate(u, t);
    return segment.evaluate(t);
}

glm::vec3 BezierPath::tangent(float u) const
{
    float t;
    const CubicBezier& segment = locate(u, t);
    return segment.derivative(t);
}

void BezierPath::buildArcLengthTable(int samplesPerSegment)
{
    // Approximate the length by a polyline through densely sampled points.
    m_tableU.clear();
    m_tableDistance.clear();
    const int total = samplesPerSegment * static_cast<int>(m_segments.size());
    glm::vec3 previous = evaluate(0.0f);
    float distance = 0.0f;
    for (int i = 0; i <= total; ++i) {
        const float u = static_cast<float>(i) / static_cast<float>(samplesPerSegment);
        const glm::vec3 p = i == total ? evaluate(0.0f) : evaluate(u); // closed loop: end on the start point
        distance += glm::length(p - previous);
        previous = p;
        m_tableU.push_back(u);
        m_tableDistance.push_back(distance);
    }
}

float BezierPath::distanceToParameter(float distance) const
{
    const float total = length();
    distance = std::fmod(distance, total);
    if (distance < 0.0f)
        distance += total;
    // First table entry with distance >= the requested one, then interpolate within that interval.
    const auto it = std::lower_bound(m_tableDistance.begin(), m_tableDistance.end(), distance);
    const size_t i = std::clamp(static_cast<size_t>(it - m_tableDistance.begin()), size_t(1), m_tableDistance.size() - 1);
    const float d0 = m_tableDistance[i - 1], d1 = m_tableDistance[i];
    const float f = d1 > d0 ? (distance - d0) / (d1 - d0) : 0.0f;
    return m_tableU[i - 1] + f * (m_tableU[i] - m_tableU[i - 1]);
}

float BezierPath::parameterToDistance(float u) const
{
    const float count = static_cast<float>(m_segments.size());
    u = std::fmod(u, count);
    if (u < 0.0f)
        u += count;
    const auto it = std::lower_bound(m_tableU.begin(), m_tableU.end(), u);
    const size_t i = std::clamp(static_cast<size_t>(it - m_tableU.begin()), size_t(1), m_tableU.size() - 1);
    const float u0 = m_tableU[i - 1], u1 = m_tableU[i];
    const float f = u1 > u0 ? (u - u0) / (u1 - u0) : 0.0f;
    return m_tableDistance[i - 1] + f * (m_tableDistance[i] - m_tableDistance[i - 1]);
}

std::vector<glm::vec3> BezierPath::sample(int samplesPerSegment) const
{
    std::vector<glm::vec3> points;
    const int total = samplesPerSegment * static_cast<int>(m_segments.size());
    for (int i = 0; i < total; ++i)
        points.push_back(evaluate(static_cast<float>(i) / static_cast<float>(samplesPerSegment)));
    return points;
}
