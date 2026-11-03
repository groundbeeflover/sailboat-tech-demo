#pragma once
#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
DISABLE_WARNINGS_POP()
#include <array>

// A sum of Gerstner (trochoidal) waves. The same numbers are evaluated twice:
//  - on the GPU in water_vert.glsl to displace the water grid, and
//  - on the CPU here, so the boat and the buoys can float on exactly the surface that is drawn.
// Keep `displacement` in sync with gerstner() in water_vert.glsl.
//
// For a rest point p on the plane, wave i has phase  theta_i = k_i (D_i . p) - omega_i t + phi_i  and moves the point by
//     dx = Q_i A_i D_i.x cos(theta_i),  dy = A_i sin(theta_i),  dz = Q_i A_i D_i.y cos(theta_i)
// (Finch 2004, "Effective Water Simulation from Physical Models", GPU Gems ch. 1.)
struct GerstnerWave {
    glm::vec2 direction { 1.0f, 0.0f }; // unit vector in xz, direction of travel
    float amplitude { 0.0f }; // metres
    float wavenumber { 1.0f }; // k = 2 pi / wavelength
    float omega { 1.0f }; // angular frequency, deep-water dispersion omega = sqrt(g k)
    float phase { 0.0f };
    float steepness { 0.0f }; // Q: 0 = plain sine, larger = sharper crests
};

class Waves {
public:
    static constexpr int MaxWaves = 8;

    // Rebuilds the wave set for this frame. Direction and strength ease towards the wind so the sea
    // turns gradually; wavelengths stay fixed (changing k would make the k.p - omega t phase jump).
    void update(float dt, float windFromRad, float windStrength, int count, float scale, bool enabled);

    // Displacement of the rest point restXZ at time t.
    glm::vec3 displacement(const glm::vec2& restXZ, float time) const;
    // Surface normal at the displaced position of restXZ.
    glm::vec3 normal(const glm::vec2& restXZ, float time) const;
    // Height of the surface above a WORLD xz position. Gerstner waves also move points sideways, so
    // we first find the rest point that ends up at worldXZ (fixed-point iteration), then take its height.
    float heightAt(const glm::vec2& worldXZ, float time) const;

    int count() const { return m_count; }
    const std::array<GerstnerWave, MaxWaves>& waves() const { return m_waves; }
    // Current (smoothed) direction the waves travel in.
    glm::vec2 mainDirection() const;

private:
    std::array<GerstnerWave, MaxWaves> m_waves {};
    int m_count { 0 };
    float m_angle { 0.0f }; // smoothed travel direction (radians)
    float m_strength { 0.0f }; // smoothed
    bool m_initialised { false };
};
