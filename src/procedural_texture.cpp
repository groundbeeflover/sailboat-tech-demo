#include "procedural_texture.h"
#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/vec3.hpp>
DISABLE_WARNINGS_POP()
#include <algorithm>
#include <cmath>

namespace {

// Integer hash -> [0, 1). Deterministic, so the textures look the same on every run.
float random01(int x, int y, int seed)
{
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u + static_cast<uint32_t>(seed) * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return static_cast<float>(h & 0xFFFFFFu) / static_cast<float>(0x1000000u);
}

int wrap(int i, int period) { return ((i % period) + period) % period; }

// Value noise on an integer lattice that repeats every `period` cells, so textures tile seamlessly.
float valueNoise(float x, float y, int periodX, int periodY, int seed)
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
float fbm(float x, float y, int periodX, int periodY, int octaves, int seed)
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

ImageRGBA makeImage(int width, int height)
{
    ImageRGBA img;
    img.width = width;
    img.height = height;
    img.pixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    return img;
}

uint8_t toByte(float v) { return static_cast<uint8_t>(std::clamp(v * 255.0f + 0.5f, 0.0f, 255.0f)); }

void setPixel(ImageRGBA& img, int x, int y, const glm::vec3& rgb)
{
    const size_t i = (static_cast<size_t>(y) * static_cast<size_t>(img.width) + static_cast<size_t>(x)) * 4;
    img.pixels[i + 0] = toByte(rgb.r);
    img.pixels[i + 1] = toByte(rgb.g);
    img.pixels[i + 2] = toByte(rgb.b);
    img.pixels[i + 3] = 255;
}

// Tangent-space normal map from a height field with central differences (wrapping, so it tiles).
// strength converts height units per pixel into slope.
ImageRGBA normalsFromHeight(const std::vector<float>& height, int width, int heightPx, float strength)
{
    ImageRGBA img = makeImage(width, heightPx);
    auto h = [&](int x, int y) { return height[static_cast<size_t>(wrap(y, heightPx)) * static_cast<size_t>(width) + static_cast<size_t>(wrap(x, width))]; };
    for (int y = 0; y < heightPx; ++y) {
        for (int x = 0; x < width; ++x) {
            const float dhdx = 0.5f * (h(x + 1, y) - h(x - 1, y));
            const float dhdy = 0.5f * (h(x, y + 1) - h(x, y - 1));
            const glm::vec3 n = glm::normalize(glm::vec3(-dhdx * strength, -dhdy * strength, 1.0f));
            setPixel(img, x, y, n * 0.5f + 0.5f);
        }
    }
    return img;
}

}

MaterialImages generateDeckPlanks(int size)
{
    constexpr int planks = 8; // 12.5 cm wide planks
    constexpr float seamHalfWidth = 1.5f; // pixels
    constexpr float bevel = 3.0f; // pixels over which a plank edge rounds off
    const float plankPx = static_cast<float>(size) / planks;

    MaterialImages out { makeImage(size, size), {}, makeImage(size, size) };
    std::vector<float> height(static_cast<size_t>(size) * static_cast<size_t>(size));

    const glm::vec3 teakLight(0.66f, 0.47f, 0.30f), teakDark(0.45f, 0.29f, 0.17f), caulking(0.06f, 0.05f, 0.045f);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size);
            const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
            const float pu = u * planks;
            const int plank = static_cast<int>(std::floor(pu));
            const float across = pu - static_cast<float>(plank);

            // Distance (pixels) to the long seams and to this plank's butt joint (one per tile, random position).
            const float jointV = random01(plank, 0, 7);
            float dv = std::abs(v - jointV);
            dv = std::min(dv, 1.0f - dv) * static_cast<float>(size);
            const float du = std::min(across, 1.0f - across) * plankPx;
            const float seamDistance = std::min(du, dv);

            // Grain: noise stretched along the plank, different per plank.
            const float grain = fbm(u * 64.0f + static_cast<float>(plank) * 5.0f, v * 4.0f, 64, 4, 4, 11);
            const float tint = 0.85f + 0.25f * random01(plank, 1, 7);

            const float wood = glm::smoothstep(seamHalfWidth, seamHalfWidth + bevel, seamDistance); // 0 in the seam, 1 on the plank
            const glm::vec3 plankColor = glm::mix(teakDark, teakLight, grain) * tint;
            setPixel(out.albedo, x, y, glm::mix(caulking, plankColor, wood));
            const float roughness = glm::mix(0.95f, 0.5f + 0.25f * (1.0f - grain), wood);
            setPixel(out.roughness, x, y, glm::vec3(roughness));
            height[static_cast<size_t>(y) * static_cast<size_t>(size) + static_cast<size_t>(x)] = wood + 0.08f * grain;
        }
    }
    out.normal = normalsFromHeight(height, size, size, 2.5f);
    return out;
}

MaterialImages generateHullStrakes(int width, int height)
{
    constexpr int strakes = 10; // planks across the girth
    constexpr float waterline = 0.5f, bootTop = 0.535f;
    const float strakePx = static_cast<float>(height) / strakes;
    const float lapEdge = 3.0f / strakePx; // fraction of a strake over which the overlap edge rises

    MaterialImages out { makeImage(width, height), {}, makeImage(width, height) };
    std::vector<float> heightField(static_cast<size_t>(width) * static_cast<size_t>(height));

    const glm::vec3 antifouling(0.45f, 0.09f, 0.07f), bootStripe(0.93f, 0.90f, 0.80f);
    const glm::vec3 mahoganyDark(0.30f, 0.11f, 0.05f), mahoganyLight(0.56f, 0.26f, 0.12f);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(width);
            const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(height);

            // Lapstrake ("clinker") planking: each strake overlaps the one below it, so its height
            // slopes down from a raised lower edge and drops at the next strake's edge.
            const float sv = v * strakes;
            const int strake = static_cast<int>(std::floor(sv));
            const float f = sv - static_cast<float>(strake);
            const float lap = f < lapEdge ? glm::mix(0.4f, 1.0f, f / lapEdge) : glm::mix(1.0f, 0.4f, (f - lapEdge) / (1.0f - lapEdge));

            const float grain = fbm(u * 24.0f, v * 160.0f, 24, 160, 4, 23);
            const float speckle = fbm(u * 128.0f, v * 64.0f, 128, 64, 2, 5);

            glm::vec3 color;
            float roughness, h;
            if (v < waterline) {
                color = antifouling * (0.9f + 0.2f * speckle);
                roughness = 0.75f + 0.15f * speckle;
                h = 0.35f * lap + 0.05f * speckle; // planks still show faintly under the paint
            } else if (v < bootTop) {
                color = bootStripe;
                roughness = 0.3f;
                h = 0.35f * lap;
            } else {
                const float tint = 0.9f + 0.2f * random01(strake, 3, 3);
                color = glm::mix(mahoganyDark, mahoganyLight, grain) * tint;
                roughness = 0.12f + 0.12f * (1.0f - grain); // varnish
                h = lap + 0.03f * grain;
            }
            setPixel(out.albedo, x, y, color);
            setPixel(out.roughness, x, y, glm::vec3(roughness));
            heightField[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)] = h;
        }
    }
    out.normal = normalsFromHeight(heightField, width, height, 3.0f);
    return out;
}

ImageRGBA generateWaterNormals(int size)
{
    // Sum of sines with integer wave vectors (so they tile) plus a bit of noise to break the regularity.
    struct Ripple {
        int kx, ky;
        float amplitude, phase;
    };
    std::vector<Ripple> ripples;
    for (int i = 0; i < 14; ++i) {
        const int kx = static_cast<int>(random01(i, 0, 99) * 14.0f) - 7;
        const int ky = static_cast<int>(random01(i, 1, 99) * 14.0f) - 7;
        if (kx == 0 && ky == 0)
            continue;
        const float k = std::sqrt(static_cast<float>(kx * kx + ky * ky));
        ripples.push_back({ kx, ky, 1.0f / k, random01(i, 2, 99) * glm::two_pi<float>() });
    }

    std::vector<float> height(static_cast<size_t>(size) * static_cast<size_t>(size));
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(size);
            const float v = static_cast<float>(y) / static_cast<float>(size);
            float h = 0.0f;
            for (const Ripple& r : ripples)
                h += r.amplitude * std::sin(glm::two_pi<float>() * (static_cast<float>(r.kx) * u + static_cast<float>(r.ky) * v) + r.phase);
            h = 0.6f * h * 0.25f + 0.4f * fbm(u * 8.0f, v * 8.0f, 8, 8, 5, 41);
            height[static_cast<size_t>(y) * static_cast<size_t>(size) + static_cast<size_t>(x)] = h;
        }
    }
    return normalsFromHeight(height, size, size, 12.0f);
}
