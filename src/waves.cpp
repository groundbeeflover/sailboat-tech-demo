#include "waves.h"
#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <glm/gtc/constants.hpp>
DISABLE_WARNINGS_POP()
#include <algorithm>
#include <cmath>

namespace {

constexpr float Gravity = 9.81f;
constexpr float GlobalSteepness = 0.65f; // sum of Q_i k_i A_i at full wind; above 1 the crests loop over
constexpr float AmplitudePerWavelength = 0.017f; // amplitude at full wind = this * wavelength

// Fixed spectrum: long swell first, then shorter wind waves spread around the main direction.
constexpr std::array<float, Waves::MaxWaves> Wavelengths { 33.0f, 21.0f, 13.5f, 8.6f, 5.7f, 3.9f, 2.7f, 1.9f };
constexpr std::array<float, Waves::MaxWaves> DirectionOffsetsDeg { 0.0f, 22.0f, -28.0f, 47.0f, -13.0f, 66.0f, -52.0f, 9.0f };
constexpr std::array<float, Waves::MaxWaves> Phases { 0.0f, 1.3f, 4.1f, 2.2f, 5.5f, 0.7f, 3.6f, 2.9f };

float wrapToPi(float a)
{
    a = std::fmod(a + glm::pi<float>(), glm::two_pi<float>());
    if (a < 0.0f)
        a += glm::two_pi<float>();
    return a - glm::pi<float>();
}

}

void Waves::update(float dt, float windFromRad, float windStrength, int count, float scale, bool enabled)
{
    // Waves travel in the direction the wind blows to, i.e. opposite to where it comes from.
    const float targetAngle = windFromRad + glm::pi<float>();
    if (!m_initialised) {
        m_angle = targetAngle;
        m_strength = windStrength;
        m_initialised = true;
    } else {
        const float maxTurn = glm::radians(25.0f) * dt;
        m_angle += std::clamp(wrapToPi(targetAngle - m_angle), -maxTurn, maxTurn);
        m_strength += std::clamp(windStrength - m_strength, -0.25f * dt, 0.25f * dt);
    }

    m_count = enabled ? std::clamp(count, 1, MaxWaves) : 0;
    for (int i = 0; i < m_count; ++i) {
        GerstnerWave& w = m_waves[static_cast<size_t>(i)];
        const float wavelength = Wavelengths[static_cast<size_t>(i)];
        const float angle = m_angle + glm::radians(DirectionOffsetsDeg[static_cast<size_t>(i)]);
        w.direction = glm::vec2(std::sin(angle), std::cos(angle));
        w.wavenumber = glm::two_pi<float>() / wavelength;
        w.omega = std::sqrt(Gravity * w.wavenumber);
        w.phase = Phases[static_cast<size_t>(i)];
        const float referenceAmplitude = AmplitudePerWavelength * wavelength;
        w.amplitude = referenceAmplitude * m_strength * scale;
        // Normalised so that sum(Q_i k_i A_i) = GlobalSteepness at full wind and scale 1
        // (Finch's Q_i = Q / (k_i A_i N), but with the full-wind amplitude so calm water does
        // not keep a large sideways motion when A_i goes to zero).
        w.steepness = GlobalSteepness / (w.wavenumber * referenceAmplitude * static_cast<float>(m_count));
    }
}

glm::vec3 Waves::displacement(const glm::vec2& restXZ, float time) const
{
    glm::vec3 d(0.0f);
    for (int i = 0; i < m_count; ++i) {
        const GerstnerWave& w = m_waves[static_cast<size_t>(i)];
        const float theta = w.wavenumber * glm::dot(w.direction, restXZ) - w.omega * time + w.phase;
        const float c = std::cos(theta);
        const float s = std::sin(theta);
        d.x += w.steepness * w.amplitude * w.direction.x * c;
        d.z += w.steepness * w.amplitude * w.direction.y * c;
        d.y += w.amplitude * s;
    }
    return d;
}

glm::vec3 Waves::normal(const glm::vec2& restXZ, float time) const
{
    glm::vec3 n(0.0f, 1.0f, 0.0f);
    for (int i = 0; i < m_count; ++i) {
        const GerstnerWave& w = m_waves[static_cast<size_t>(i)];
        const float theta = w.wavenumber * glm::dot(w.direction, restXZ) - w.omega * time + w.phase;
        const float kA = w.wavenumber * w.amplitude;
        n.x -= w.direction.x * kA * std::cos(theta);
        n.z -= w.direction.y * kA * std::cos(theta);
        n.y -= w.steepness * kA * std::sin(theta);
    }
    return glm::normalize(n);
}

float Waves::heightAt(const glm::vec2& worldXZ, float time) const
{
    // Solve rest + horizontal(rest) = worldXZ. The horizontal displacement is a contraction as long as
    // the crests do not loop (sum Q k A < 1), so a few iterations converge.
    glm::vec2 rest = worldXZ;
    for (int it = 0; it < 4; ++it) {
        const glm::vec3 d = displacement(rest, time);
        rest = worldXZ - glm::vec2(d.x, d.z);
    }
    return displacement(rest, time).y;
}

glm::vec2 Waves::mainDirection() const
{
    return glm::vec2(std::sin(m_angle), std::cos(m_angle));
}
