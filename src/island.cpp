#include "island.h"
#include "procedural_mesh.h"
#include "sky.h"
#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
DISABLE_WARNINGS_POP()
#include <cmath>

namespace {

// Dimensions in metres, island frame: origin at the waterline below the lighthouse, +y up.
constexpr float TerrainRadius = 26.0f;
constexpr float TerrainHeight = 7.0f; // from its lowest point to the top
constexpr float TerrainSink = 2.0f; // below the waterline, so the waves surround it
constexpr float TerrainTop = TerrainHeight - TerrainSink;
constexpr float TowerHeight = 16.0f;
constexpr float TowerRadiusBottom = 2.0f;
constexpr float TowerRadiusTop = 1.4f;
constexpr int TowerBands = 6; // alternating white and red, from the bottom
constexpr float GalleryRadius = 2.1f;
constexpr float GalleryThickness = 0.35f;
constexpr float LanternRadius = 1.1f;
constexpr float LanternHeight = 2.0f;
constexpr float LampHeight = 1.0f; // above the gallery floor

// The lamp's beam leaves along its +z, tilted below the horizon so it reaches the water around the
// course (the lamp is about 22 m up and the course about 110 m away: 11 deg down, inside the cone).
constexpr float BeamTilt = 0.105f; // radians, 6 deg
constexpr glm::vec3 LampColor { 1.0f, 0.9f, 0.7f };
constexpr float LampIntensity = 20000.0f; // the spotlight falls off with 1/d^2: about 2 at the course
constexpr glm::vec3 LampEmission { 14.0f, 11.0f, 6.0f }; // HDR, well above the bloom threshold

glm::mat4 translate(const glm::vec3& t) { return glm::translate(glm::mat4(1.0f), t); }
glm::mat4 rotateY(float a) { return glm::rotate(glm::mat4(1.0f), a, glm::vec3(0, 1, 0)); }

}

GPUMesh* Island::addMesh(const Mesh& mesh)
{
    m_meshes.push_back(std::make_unique<GPUMesh>(mesh));
    return m_meshes.back().get();
}

Island::Island(const glm::vec3& position)
{
    // ---- Materials
    m_rockMaterial = { .albedo = glm::vec3(0.36f, 0.33f, 0.29f), .roughness = 0.9f };
    m_whiteMaterial = { .albedo = glm::vec3(0.9f, 0.9f, 0.87f), .roughness = 0.6f };
    m_redMaterial = { .albedo = glm::vec3(0.7f, 0.06f, 0.04f), .roughness = 0.6f };
    m_metalMaterial = { .albedo = glm::vec3(0.12f, 0.13f, 0.12f), .roughness = 0.4f, .metallic = 1.0f };
    m_lampMaterial = { .albedo = glm::vec3(0.9f, 0.85f, 0.6f), .roughness = 0.15f }; // emissive is set in update()

    // ---- Meshes
    // Placeholder terrain: a flattened dome, sunk so it crosses the waterline. The fbm heightfield
    // (TODO A1 step 1) replaces this mesh; keep its top at TerrainTop under the lighthouse.
    Mesh dome = makeSphere(1.0f, 48, 24);
    transformMesh(dome, translate(glm::vec3(0.0f, -TerrainSink, 0.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(TerrainRadius, TerrainHeight, TerrainRadius)));
    GPUMesh* terrainMesh = addMesh(dome);

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
    m_root->addChild("Terrain", terrainMesh, &m_rockMaterial);
    // Sunk a little, so the tower base meets the curved top of the terrain.
    SceneNode& lighthouse = m_root->addChild("Lighthouse", nullptr, nullptr, translate(glm::vec3(0.0f, TerrainTop - 0.3f, 0.0f)));
    SceneNode& tower = lighthouse.addChild("Tower", towerMesh, &m_whiteMaterial);
    tower.addChild("Bands", bandsMesh, &m_redMaterial);
    SceneNode& gallery = tower.addChild("Gallery", galleryMesh, &m_metalMaterial, translate(glm::vec3(0.0f, TowerHeight, 0.0f)));
    SceneNode& lampHouse = gallery.addChild("LampHouse", lanternMesh, &m_metalMaterial, translate(glm::vec3(0.0f, GalleryThickness, 0.0f)));
    m_lamp = &lampHouse.addChild("Lamp", lampMesh, &m_lampMaterial, translate(glm::vec3(0.0f, LampHeight, 0.0f)));

    m_root->updateWorld();
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
    light.direction = glm::normalize(glm::vec3(m_lamp->world * glm::vec4(0.0f, -std::sin(BeamTilt), std::cos(BeamTilt), 0.0f)));
    light.color = m_night * LampIntensity * LampColor;
    return light;
}
