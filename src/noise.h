#pragma once
#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <glm/common.hpp>
DISABLE_WARNINGS_POP()
#include <cmath>
#include <cstdint>

// Value noise and fBm, shared by the procedural textures and the island terrain. Pure functions:
// the same inputs give the same output on every run and every platform.

// Integer hash -> [0, 1). Deterministic, so the textures look the same on every run.
inline float random01(int x, int y, int seed)
{
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u + static_cast<uint32_t>(seed) * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return static_cast<float>(h & 0xFFFFFFu) / static_cast<float>(0x1000000u);
}

inline int wrap(int i, int period) { return ((i % period) + period) % period; }

// Value noise on an integer lattice that repeats every `period` cells, so textures tile seamlessly.
inline float valueNoise(float x, float y, int periodX, int periodY, int seed)
{
    const int xi = static_cast<int>(std::floor(x));
    const int yi = static_cast<int>(std::floor(y));
    const float fx = x - static_cast<float>(xi);
    const float fy = y - static_cast<float>(yi);
    const float sx = fx * fx * (3.0f - 2.0f * fx);
    const float sy = fy * fy * (3.0f - 2.0f * fy);
    auto h = [&](int ix, int iy) { return random01(wrap(ix, periodX), wrap(iy, periodY), seed); };
    const float a = glm::mix(h(xi, yi), h(xi + 1, yi), sx);
    const float b = glm::mix(h(xi, yi + 1), h(xi + 1, yi + 1), sx);
    return glm::mix(a, b, sy);
}

// Fractal sum of value noise; each octave doubles frequency and period, so tiling is preserved.
// The result stays in [0, 1] (most values near 0.5).
inline float fbm(float x, float y, int periodX, int periodY, int octaves, int seed)
{
    float sum = 0.0f, amplitude = 0.5f, norm = 0.0f;
    for (int o = 0; o < octaves; ++o) {
        const int scale = 1 << o;
        sum += amplitude * valueNoise(x * static_cast<float>(scale), y * static_cast<float>(scale), periodX * scale, periodY * scale, seed + o);
        norm += amplitude;
        amplitude *= 0.5f;
    }
    return sum / norm;
}
