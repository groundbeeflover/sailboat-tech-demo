#include "procedural_texture.h"
#include "noise.h"
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

MaterialImages generateTerrain(const TerrainSamples& terrain, int size)
{
    MaterialImages out { makeImage(size, size), {}, makeImage(size, size) };
    std::vector<float> bumps(static_cast<size_t>(size) * static_cast<size_t>(size)); // metres, for the normal map
    const int last = terrain.resolution - 1;

    const glm::vec3 drySand(0.78f, 0.71f, 0.56f), wetSand(0.46f, 0.41f, 0.32f);
    const glm::vec3 grassLush(0.27f, 0.38f, 0.14f), grassDry(0.52f, 0.51f, 0.27f);
    const glm::vec3 rockLight(0.50f, 0.47f, 0.42f), rockDark(0.24f, 0.22f, 0.20f), lichen(0.47f, 0.47f, 0.32f);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size);
            const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);

            // Bilinear lookup in the sample grid, which is much coarser than the texture.
            const float gx = u * static_cast<float>(last), gy = v * static_cast<float>(last);
            const int i = std::min(static_cast<int>(gx), last - 1), j = std::min(static_cast<int>(gy), last - 1);
            const float fx = gx - static_cast<float>(i), fy = gy - static_cast<float>(j);
            auto at = [&](int di, int dj) { return static_cast<size_t>(j + dj) * static_cast<size_t>(terrain.resolution) + static_cast<size_t>(i + di); };
            const float h = glm::mix(glm::mix(terrain.height[at(0, 0)], terrain.height[at(1, 0)], fx), glm::mix(terrain.height[at(0, 1)], terrain.height[at(1, 1)], fx), fy);
            const glm::vec3 n = glm::normalize(glm::mix(glm::mix(terrain.normal[at(0, 0)], terrain.normal[at(1, 0)], fx), glm::mix(terrain.normal[at(0, 1)], terrain.normal[at(1, 1)], fx), fy));
            if (h < -2.5f) {
                // Deeper than any wave trough, and the water is opaque: never seen, so skip the noise
                // there (more than half of the texture).
                setPixel(out.albedo, x, y, wetSand);
                setPixel(out.roughness, x, y, glm::vec3(0.9f));
                continue; // bumps stay 0
            }

            // Noise in metres: ~10 m patches shift the borders and dry out the grass, ~1 m grain speckles everything.
            const float px = u * terrain.size, pz = v * terrain.size;
            const float patches = fbm(px / 9.0f, pz / 9.0f, 64, 64, 3, 301);
            const float grain = fbm(px / 0.7f, pz / 0.7f, 256, 256, 3, 302);

            // Material weights. slope = 1 - n.y: 0 on flat ground, 0.13 at 30 degrees, 0.22 at 39 degrees.
            const float slope = 1.0f - n.y;
            const float rock = glm::smoothstep(0.13f, 0.22f, slope + 0.1f * (patches - 0.5f));
            const float sand = 1.0f - glm::smoothstep(0.8f, 1.8f, h + 2.0f * (patches - 0.5f));
            const float wet = 1.0f - glm::smoothstep(0.0f, 0.5f, h); // the waves wash up to about here

            // Rock layers follow the contour lines, so on a cliff they read as strata; the noise in
            // the phase keeps them from looking like a contour map. Seen from above, a steep face gets
            // few texels, so the (x, z) noise stretches down it; the layers depend on the height
            // instead and don't, so steep faces use mostly layers. Lichen in patches.
            const float strata = 0.5f + 0.5f * std::sin(h * 2.3f + 7.0f * patches);
            const float layered = glm::smoothstep(0.3f, 0.6f, slope);
            const glm::vec3 bareRock = glm::mix(rockDark, rockLight, glm::mix(0.3f * strata + 0.7f * grain, 0.8f * strata + 0.2f * grain, layered));
            const glm::vec3 rockColor = glm::mix(bareRock, lichen * (0.8f + 0.4f * grain), 0.35f * glm::smoothstep(0.55f, 0.7f, patches));
            const glm::vec3 grassColor = glm::mix(grassLush, grassDry, glm::smoothstep(0.35f, 0.65f, patches)) * (0.85f + 0.3f * grain);
            const glm::vec3 sandColor = glm::mix(drySand, wetSand, wet) * (0.92f + 0.16f * grain);
            setPixel(out.albedo, x, y, glm::mix(glm::mix(grassColor, sandColor, sand), rockColor, rock));

            const float groundRoughness = glm::mix(0.9f, glm::mix(0.95f, 0.4f, wet), sand); // wet sand is shiny
            setPixel(out.roughness, x, y, glm::vec3(glm::mix(groundRoughness, 0.65f + 0.25f * grain, rock)));

            // Boulder-sized bumps on rock, fine grain elsewhere.
            const float rockBumps = rock > 0.0f ? fbm(px / 1.5f, pz / 1.5f, 128, 128, 4, 303) : 0.0f;
            bumps[static_cast<size_t>(y) * static_cast<size_t>(size) + static_cast<size_t>(x)] = glm::mix(0.04f * grain, 0.35f * rockBumps, rock);
        }
    }
    // strength = pixels per metre, so height differences in metres become slopes.
    out.normal = normalsFromHeight(bumps, size, size, static_cast<float>(size) / terrain.size);
    return out;
}
