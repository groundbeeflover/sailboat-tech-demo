#include "island.h"
#include "noise.h"
#include "procedural_mesh.h"
#include "sky.h"
#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec2.hpp>
DISABLE_WARNINGS_POP()
#include <algorithm>
#include <cmath>

namespace {

// Dimensions in metres, island frame: origin on the waterline at the island's centre, +y up.

// Terrain
constexpr float TerrainSize = 120.0f; // side of the square grid
constexpr int TerrainCells = 192; // per side: 0.625 m between vertices
constexpr float IslandRadius = 52.0f; // where the falloff reaches the seabed (before the coast noise scales it)
constexpr float SeabedDepth = 5.0f; // below the waterline: deeper than any wave trough, so the grid edge never shows
constexpr float ReliefHeight = 16.0f; // A: height range of the fBm relief
constexpr float BaseHeight = 2.0f; // B: lifts the relief, so even its low spots are land
constexpr float PlateauRadius = 5.0f; // levelled ground for the lighthouse
constexpr float PlateauBlend = 7.0f; // distance over which the plateau blends back into the natural terrain

// Lighthouse
constexpr float TowerHeight = 16.0f;
constexpr float TowerRadiusBottom = 2.0f;
constexpr float TowerRadiusTop = 1.4f;
constexpr int TowerBands = 6; // alternating white and red, from the bottom
constexpr float GalleryRadius = 2.1f;
constexpr float GalleryThickness = 0.35f;
constexpr float LanternRadius = 1.1f;
constexpr float LanternHeight = 2.0f;
constexpr float LampHeight = 1.0f; // above the gallery floor

// The beam leaves the lamp along its +z, aimed down at the water this far out. The course runs
// 95-140 m from the centre, so the beam sweeps over it (a real lighthouse aims at the horizon).
constexpr float BeamReach = 110.0f;
constexpr glm::vec3 LampColor { 1.0f, 0.9f, 0.7f };
constexpr float LampIntensity = 20000.0f; // the spotlight falls off with 1/d^2: about 1.5 at the course
constexpr glm::vec3 LampEmission { 14.0f, 11.0f, 6.0f }; // HDR, well above the bloom threshold

glm::mat4 translate(const glm::vec3& t) { return glm::translate(glm::mat4(1.0f), t); }
glm::mat4 rotateY(float a) { return glm::rotate(glm::mat4(1.0f), a, glm::vec3(0, 1, 0)); }

// The island before the lighthouse site is levelled: fBm relief, blended into a flat seabed by a
// radial falloff,
//     h = mix(-SeabedDepth, A * fbm(p) + B, falloff(r)),
// so the surface crosses the waterline somewhere on the way out: that is the coast. Low-frequency
// noise on r bends the coastline into bays and headlands, and more noise moves the start of the
// falloff: where it starts late the drop is short (cliffs), where it starts early the slope is long
// and gentle (grass down to a beach).
float naturalHeight(const glm::vec2& p)
{
    const float coast = fbm(p.x / 40.0f + 17.0f, p.y / 40.0f + 31.0f, 64, 64, 2, 401);
    const float cliffs = fbm(p.x / 30.0f + 3.0f, p.y / 30.0f + 23.0f, 64, 64, 2, 403);
    const float r = glm::length(p) / IslandRadius * (0.6f + 0.8f * coast);
    const float falloffStart = glm::mix(0.05f, 0.68f, glm::smoothstep(0.35f, 0.65f, cliffs));
    const float falloff = 1.0f - glm::smoothstep(falloffStart, 1.0f, r);
    const float relief = fbm(p.x / 26.0f + 5.0f, p.y / 26.0f + 11.0f, 64, 64, 5, 402);
    return glm::mix(-SeabedDepth, ReliefHeight * relief + BaseHeight, falloff);
}

// The terrain as built: the natural shape with a level plateau for the lighthouse in the middle.
float terrainHeight(const glm::vec2& p)
{
    const float natural = naturalHeight(p);
    const float blend = glm::smoothstep(PlateauRadius, PlateauRadius + PlateauBlend, glm::length(p)); // 0 on the plateau
    return blend >= 1.0f ? natural : glm::mix(naturalHeight(glm::vec2(0.0f)), natural, blend);
}

// Normal of the surface y = h(x, z) from central differences: n ~ (-dh/dx, 1, -dh/dz).
glm::vec3 terrainNormal(const glm::vec2& p, float step)
{
    const float dhdx = (terrainHeight(p + glm::vec2(step, 0.0f)) - terrainHeight(p - glm::vec2(step, 0.0f))) / (2.0f * step);
    const float dhdz = (terrainHeight(p + glm::vec2(0.0f, step)) - terrainHeight(p - glm::vec2(0.0f, step))) / (2.0f * step);
    return glm::normalize(glm::vec3(-dhdx, 1.0f, -dhdz));
}

glm::vec2 gridPoint(int i, int j) { return (glm::vec2(static_cast<float>(i), static_cast<float>(j)) / static_cast<float>(TerrainCells) - 0.5f) * TerrainSize; }

TerrainSamples sampleTerrain()
{
    TerrainSamples samples { .resolution = TerrainCells + 1, .size = TerrainSize };
    for (int j = 0; j <= TerrainCells; ++j) {
        for (int i = 0; i <= TerrainCells; ++i) {
            samples.height.push_back(terrainHeight(gridPoint(i, j)));
            samples.normal.push_back(terrainNormal(gridPoint(i, j), TerrainSize / TerrainCells));
        }
    }
    return samples;
}

// One vertex per sample, two triangles per cell (counter-clockwise seen from above). The texture
// coordinates span the whole grid, because the baked maps cover the island exactly once.
Mesh makeTerrainMesh(const TerrainSamples& samples)
{
    Mesh mesh;
    mesh.material.kd = glm::vec3(1.0f); // unused by the PBR path, but GPUMesh copies it
    const auto res = static_cast<uint32_t>(samples.resolution);
    for (int j = 0; j <= TerrainCells; ++j) {
        for (int i = 0; i <= TerrainCells; ++i) {
            const size_t k = static_cast<size_t>(j) * res + static_cast<size_t>(i);
            const glm::vec2 p = gridPoint(i, j);
            const glm::vec2 uv = glm::vec2(static_cast<float>(i), static_cast<float>(j)) / static_cast<float>(TerrainCells);
            mesh.vertices.push_back({ glm::vec3(p.x, samples.height[k], p.y), samples.normal[k], uv });
        }
    }
    for (uint32_t j = 0; j < res - 1; ++j) {
        for (uint32_t i = 0; i < res - 1; ++i) {
            const uint32_t a = j * res + i, b = a + 1, c = a + res, d = c + 1;
            mesh.triangles.push_back({ a, c, b });
            mesh.triangles.push_back({ b, c, d });
        }
    }
    return mesh;
}

}

GPUMesh* Island::addMesh(const Mesh& mesh)
{
    m_meshes.push_back(std::make_unique<GPUMesh>(mesh));
    return m_meshes.back().get();
}

Texture* Island::addTexture(const ImageRGBA& image, bool srgb)
{
    m_textures.push_back(std::make_unique<Texture>(image.width, image.height, image.pixels, srgb));
    return m_textures.back().get();
}

Island::Island(const glm::vec3& position)
    : m_position(position)
{
    // ---- Terrain: sampled once, then the mesh and the material maps are both built from the samples.
    const TerrainSamples samples = sampleTerrain();
    for (int j = 0; j <= TerrainCells; ++j) {
        for (int i = 0; i <= TerrainCells; ++i) {
            if (samples.height[static_cast<size_t>(j) * static_cast<size_t>(samples.resolution) + static_cast<size_t>(i)] > 0.0f)
                m_shoreRadius = std::max(m_shoreRadius, glm::length(gridPoint(i, j)));
        }
    }
    GPUMesh* terrainMesh = addMesh(makeTerrainMesh(samples));
    const MaterialImages terrainImages = generateTerrain(samples);
    m_terrainMaterial.albedo = glm::vec3(1.0f);
    m_terrainMaterial.roughness = 1.0f;
    m_terrainMaterial.albedoMap = addTexture(terrainImages.albedo, true);
    m_terrainMaterial.normalMap = addTexture(terrainImages.normal, false);
    m_terrainMaterial.roughnessMap = addTexture(terrainImages.roughness, false);

    // ---- Lighthouse materials
    m_whiteMaterial = { .albedo = glm::vec3(0.9f, 0.9f, 0.87f), .roughness = 0.6f };
    m_redMaterial = { .albedo = glm::vec3(0.7f, 0.06f, 0.04f), .roughness = 0.6f };
    m_metalMaterial = { .albedo = glm::vec3(0.12f, 0.13f, 0.12f), .roughness = 0.4f, .metallic = 1.0f };
    m_lampMaterial = { .albedo = glm::vec3(0.9f, 0.85f, 0.6f), .roughness = 0.15f }; // emissive is set in update()

    // ---- Lighthouse meshes
    // Tower: a tapered stack of bands, white ones in one mesh and red ones in the other.
    std::vector<Mesh> whiteBands, redBands;
    for (int i = 0; i < TowerBands; ++i) {
        const float t0 = static_cast<float>(i) / TowerBands;
        const float t1 = static_cast<float>(i + 1) / TowerBands;
        Mesh band = makeCylinder(glm::mix(TowerRadiusBottom, TowerRadiusTop, t0), glm::mix(TowerRadiusBottom, TowerRadiusTop, t1), (t1 - t0) * TowerHeight, 32);
        transformMesh(band, translate(glm::vec3(0.0f, t0 * TowerHeight, 0.0f)));
        (i % 2 == 0 ? whiteBands : redBands).push_back(band);
    }
    GPUMesh* towerMesh = addMesh(mergeMeshList(whiteBands));
    GPUMesh* bandsMesh = addMesh(mergeMeshList(redBands));
    GPUMesh* galleryMesh = addMesh(makeCylinder(GalleryRadius, GalleryRadius, GalleryThickness, 32));

    // Lantern room: open posts under a conical roof, so the turning lamp stays visible.
    std::vector<Mesh> lanternParts;
    for (int i = 0; i < 6; ++i) {
        const float a = static_cast<float>(i) / 6.0f * glm::two_pi<float>();
        const glm::vec3 foot(LanternRadius * std::cos(a), 0.0f, LanternRadius * std::sin(a));
        lanternParts.push_back(makeCylinderBetween(foot, foot + glm::vec3(0.0f, LanternHeight, 0.0f), 0.06f));
    }
    Mesh roof = makeCylinder(1.45f, 0.12f, 1.1f);
    transformMesh(roof, translate(glm::vec3(0.0f, LanternHeight, 0.0f)));
    lanternParts.push_back(roof);
    Mesh finial = makeSphere(0.2f);
    transformMesh(finial, translate(glm::vec3(0.0f, LanternHeight + 1.15f, 0.0f)));
    lanternParts.push_back(finial);
    GPUMesh* lanternMesh = addMesh(mergeMeshList(lanternParts));

    // Lamp: longer along +z (the beam direction), so its rotation is easy to see.
    GPUMesh* lampMesh = addMesh(makeBox(glm::vec3(0.3f, 0.4f, 0.65f)));

    // ---- Hierarchy
    m_root = std::make_unique<SceneNode>("Island", nullptr, nullptr, translate(position));
    m_root->addChild("Terrain", terrainMesh, &m_terrainMaterial);
    // Sunk a little into the plateau, so no gap shows under the tower.
    SceneNode& lighthouse = m_root->addChild("Lighthouse", nullptr, nullptr, translate(glm::vec3(0.0f, terrainHeight(glm::vec2(0.0f)) - 0.2f, 0.0f)));
    SceneNode& tower = lighthouse.addChild("Tower", towerMesh, &m_whiteMaterial);
    tower.addChild("Bands", bandsMesh, &m_redMaterial);
    SceneNode& gallery = tower.addChild("Gallery", galleryMesh, &m_metalMaterial, translate(glm::vec3(0.0f, TowerHeight, 0.0f)));
    SceneNode& lampHouse = gallery.addChild("LampHouse", lanternMesh, &m_metalMaterial, translate(glm::vec3(0.0f, GalleryThickness, 0.0f)));
    m_lamp = &lampHouse.addChild("Lamp", lampMesh, &m_lampMaterial, translate(glm::vec3(0.0f, LampHeight, 0.0f)));
    // The spotlight originates inside this solid box. If it is rendered into its own
    // depth map, the front face blocks the entire narrow cone (even with depth bias).
    m_lamp->castsSpotShadow = false;

    m_root->updateWorld();
    m_beamTilt = std::atan2(m_lamp->world[3].y, BeamReach);
}

void Island::update(float dt, const FeatureToggles& toggles, const Lighting& lighting)
{
    m_root->visible = toggles.island;

    // Integrated rather than speed * time, so moving the speed slider doesn't make the beam jump.
    m_lampAngle = std::fmod(m_lampAngle + toggles.lighthouseSpeed * dt, glm::two_pi<float>());
    m_lamp->anim = rotateY(m_lampAngle);

    // On from just before sunset to just after sunrise, faded so the day-night cycle doesn't pop.
    m_night = 1.0f - glm::smoothstep(0.0f, 0.12f, lighting.sunDirection.y);
    m_lampMaterial.emissive = m_night * LampEmission;

    m_root->updateWorld();
}

SpotLight Island::lampLight() const
{
    SpotLight light;
    light.enabled = lampOn();
    light.position = glm::vec3(m_lamp->world[3]);
    light.direction = glm::normalize(glm::vec3(m_lamp->world * glm::vec4(0.0f, -std::sin(m_beamTilt), std::cos(m_beamTilt), 0.0f)));
    light.color = m_night * LampIntensity * LampColor;
    return light;
}
