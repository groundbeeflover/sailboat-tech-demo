#include "boat.h"
#include "bezier.h"
#include "procedural_texture.h"
#include "waves.h"
#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
DISABLE_WARNINGS_POP()
#include <algorithm>
#include <array>
#include <cmath>

namespace {

// Rig dimensions (metres, boat frame: +z forward, +y up, +x port).
constexpr float MastZ = 0.8f;
constexpr float MastHeight = 8.6f;
constexpr float BoomHeight = 1.15f; // above the mast foot
constexpr float BoomLength = 3.4f;
constexpr float SailFoot = 3.25f;
constexpr float SailLuff = MastHeight - BoomHeight - 0.25f;
constexpr float CabinZ = -0.675f;
constexpr glm::vec3 CabinHalfExtents { 0.75f, 0.25f, 1.025f };
constexpr float DeckCameraZ = -3.3f;

glm::mat4 translate(const glm::vec3& t) { return glm::translate(glm::mat4(1.0f), t); }
glm::mat4 rotateX(float a) { return glm::rotate(glm::mat4(1.0f), a, glm::vec3(1, 0, 0)); }
glm::mat4 rotateY(float a) { return glm::rotate(glm::mat4(1.0f), a, glm::vec3(0, 1, 0)); }
glm::mat4 rotateZ(float a) { return glm::rotate(glm::mat4(1.0f), a, glm::vec3(0, 0, 1)); }

float wrapToPi(float a)
{
    a = std::fmod(a + glm::pi<float>(), glm::two_pi<float>());
    if (a < 0.0f)
        a += glm::two_pi<float>();
    return a - glm::pi<float>();
}

// Exponential smoothing towards a target with time constant tau, independent of the frame rate.
void smoothTowards(float& value, float target, float dt, float tau)
{
    value += (target - value) * (1.0f - std::exp(-dt / tau));
}

// Relative boat speed for a given angle to the wind (a very simplified "polar diagram"):
// slow when pointing into the wind (no-go zone), fastest on a beam reach, a bit slower downwind.
float pointOfSailFactor(float windAngle)
{
    const float deg = glm::degrees(windAngle);
    return 0.15f + 0.85f * glm::smoothstep(25.0f, 80.0f, deg) * (1.0f - 0.3f * glm::smoothstep(110.0f, 180.0f, deg));
}

}

GPUMesh* Boat::addMesh(const Mesh& mesh)
{
    m_meshes.push_back(std::make_unique<GPUMesh>(mesh));
    return m_meshes.back().get();
}

Texture* Boat::addTexture(const ImageRGBA& image, bool srgb)
{
    m_textures.push_back(std::make_unique<Texture>(image.width, image.height, image.pixels, srgb));
    return m_textures.back().get();
}

Boat::Boat()
{
    // ---- Materials (textures are generated, see procedural_texture.cpp)
    const MaterialImages hullImages = generateHullStrakes();
    m_hullMaterial.albedo = glm::vec3(1.0f);
    m_hullMaterial.roughness = 1.0f;
    m_hullMaterial.albedoMap = addTexture(hullImages.albedo, true);
    m_hullMaterial.normalMap = addTexture(hullImages.normal, false);
    m_hullMaterial.roughnessMap = addTexture(hullImages.roughness, false);

    const MaterialImages deckImages = generateDeckPlanks();
    m_deckMaterial.albedo = glm::vec3(1.0f);
    m_deckMaterial.roughness = 1.0f;
    m_deckMaterial.albedoMap = addTexture(deckImages.albedo, true);
    m_deckMaterial.normalMap = addTexture(deckImages.normal, false);
    m_deckMaterial.roughnessMap = addTexture(deckImages.roughness, false);

    m_cabinMaterial = { .albedo = glm::vec3(0.86f, 0.86f, 0.83f), .roughness = 0.3f };
    m_aluminiumMaterial = { .albedo = glm::vec3(0.91f, 0.92f, 0.93f), .roughness = 0.3f, .metallic = 1.0f };
    m_steelMaterial = { .albedo = glm::vec3(0.75f, 0.75f, 0.77f), .roughness = 0.2f, .metallic = 1.0f };
    m_sailMaterial = { .albedo = glm::vec3(0.93f, 0.91f, 0.86f), .roughness = 0.75f, .doubleSided = true };
    m_varnishMaterial = { .albedo = glm::vec3(0.45f, 0.2f, 0.09f), .roughness = 0.25f };
    m_vaneMaterial = { .albedo = glm::vec3(0.85f, 0.08f, 0.04f), .roughness = 0.4f };

    // ---- Meshes
    const float mastFootY = m_shape.sheer(m_shape.uAt(MastZ)) + 0.03f;
    const float sternZ = m_shape.z(0.0f);
    const glm::vec3 masthead(0.0f, mastFootY + MastHeight - 0.1f, MastZ);

    GPUMesh* hullMesh = addMesh(makeHull(m_shape));
    GPUMesh* deckMesh = addMesh(makeDeck(m_shape));
    GPUMesh* cabinMesh = addMesh(makeBox(CabinHalfExtents));
    GPUMesh* mastMesh = addMesh(makeCylinder(0.075f, 0.05f, MastHeight));
    GPUMesh* boomMesh = addMesh(makeCylinderBetween(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -BoomLength), 0.05f));
    GPUMesh* sailMesh = addMesh(makeSail(SailFoot, SailLuff));

    // Rudder blade + stock, modelled around the pivot (origin) so the node can simply rotate about y.
    Mesh blade = makeBox(glm::vec3(0.025f, 0.55f, 0.2f));
    transformMesh(blade, translate(glm::vec3(0.0f, -0.45f, -0.2f)));
    const std::array<Mesh, 2> rudderParts { blade, makeCylinderBetween(glm::vec3(0.0f, 0.1f, 0.0f), glm::vec3(0.0f, 0.5f, 0.0f), 0.03f) };
    GPUMesh* rudderMesh = addMesh(mergeMeshList(rudderParts));
    GPUMesh* tillerMesh = addMesh(makeCylinderBetween(glm::vec3(0.0f), glm::vec3(0.0f, 0.08f, 1.4f), 0.03f));

    // Wind vane: thin pointer forward (+z, into the wind) and a larger tail fin behind the spindle.
    Mesh pointer = makeBox(glm::vec3(0.008f, 0.025f, 0.2f));
    transformMesh(pointer, translate(glm::vec3(0.0f, 0.12f, 0.1f)));
    Mesh fin = makeBox(glm::vec3(0.008f, 0.1f, 0.08f));
    transformMesh(fin, translate(glm::vec3(0.0f, 0.15f, -0.16f)));
    const std::array<Mesh, 3> vaneParts { pointer, fin, makeCylinderBetween(glm::vec3(0.0f), glm::vec3(0.0f, 0.2f, 0.0f), 0.01f) };
    GPUMesh* vaneMesh = addMesh(mergeMeshList(vaneParts));

    // Standing rigging: forestay, backstay and two shrouds. Static relative to the hull.
    std::vector<Mesh> wires;
    wires.push_back(makeCylinderBetween(masthead, glm::vec3(0.0f, m_shape.sheer(1.0f) + 0.05f, m_shape.z(1.0f) - 0.08f), 0.012f));
    wires.push_back(makeCylinderBetween(masthead, glm::vec3(0.0f, m_shape.sheer(0.0f) + 0.05f, sternZ + 0.1f), 0.012f));
    const float chainplateZ = MastZ - 0.3f;
    const float chainplateX = m_shape.halfBeam(m_shape.uAt(chainplateZ)) - 0.05f;
    const float chainplateY = m_shape.sheer(m_shape.uAt(chainplateZ)) + 0.02f;
    for (float side : { -1.0f, 1.0f })
        wires.push_back(makeCylinderBetween(glm::vec3(0.0f, mastFootY + 6.5f, MastZ), glm::vec3(side * chainplateX, chainplateY, chainplateZ), 0.012f));
    GPUMesh* riggingMesh = addMesh(mergeMeshList(wires));

    // ---- Hierarchy
    m_root = std::make_unique<SceneNode>("Boat");
    SceneNode& hull = m_root->addChild("Hull", hullMesh, &m_hullMaterial);
    hull.addChild("Deck", deckMesh, &m_deckMaterial);
    const float cabinY = m_shape.sheer(m_shape.uAt(CabinZ)) + CabinHalfExtents.y - 0.03f;
    hull.addChild("Cabin", cabinMesh, &m_cabinMaterial, translate(glm::vec3(0.0f, cabinY, CabinZ)));
    hull.addChild("Rigging", riggingMesh, &m_steelMaterial);

    m_rudder = &hull.addChild("Rudder", rudderMesh, &m_varnishMaterial, translate(glm::vec3(0.0f, 0.35f, sternZ - 0.04f)));
    m_rudder->addChild("Tiller", tillerMesh, &m_varnishMaterial, translate(glm::vec3(0.0f, 0.5f, 0.0f)));

    SceneNode& mast = hull.addChild("Mast", mastMesh, &m_aluminiumMaterial, translate(glm::vec3(0.0f, mastFootY, MastZ)));
    m_boom = &mast.addChild("Boom", boomMesh, &m_aluminiumMaterial, translate(glm::vec3(0.0f, BoomHeight, 0.0f)));
    // The sail's luff sits just behind the mast, its foot along the boom.
    m_sail = &m_boom->addChild("Sail", sailMesh, &m_sailMaterial, translate(glm::vec3(0.0f, 0.05f, -0.08f)));
    m_vane = &mast.addChild("WindVane", vaneMesh, &m_vaneMaterial, translate(glm::vec3(0.0f, MastHeight, 0.0f)));

    // Cameras look down their local -z, so turn it around to look forward (+z), tilted down a little.
    // Eye height is above the boom, slightly to port of the centreline.
    const float cameraY = m_shape.sheer(m_shape.uAt(DeckCameraZ)) + 2.0f;
    m_deckCamera = &hull.addChild("DeckCamera", nullptr, nullptr,
        translate(glm::vec3(0.5f, cameraY, DeckCameraZ)) * rotateY(glm::pi<float>()) * rotateX(glm::radians(-7.0f)));
}

void Boat::update(float dt, float time, const FeatureToggles& toggles, const BezierPath& course, const Waves& waves)
{
    // ---- 1. Move along the course
    const float speedFactor = toggles.windAffectsSpeed ? pointOfSailFactor(m_windAngle) * (0.4f + 0.6f * toggles.windStrength) : 1.0f;
    const float speed = toggles.pauseBoat ? 0.0f : toggles.boatSpeed * speedFactor;
    const float segments = static_cast<float>(course.segmentCount());
    if (toggles.constantSpeed) {
        // Advance the arc length and look up the matching parameter: equal distance per second.
        m_distance = std::fmod(m_distance + speed * dt, course.length());
        m_u = course.distanceToParameter(m_distance);
    } else {
        // Naive: advance the Bezier parameter at a constant rate (same average speed). The boat now
        // speeds up where control points are spread out and slows down where they bunch up.
        m_u = std::fmod(m_u + speed * dt / (course.length() / segments), segments);
        m_distance = course.parameterToDistance(m_u);
    }

    const glm::vec3 position = course.evaluate(m_u);
    const glm::vec3 tangent = course.tangent(m_u);
    const float yaw = std::atan2(tangent.x, tangent.z);
    if (!m_initialised) {
        m_position = position;
        m_yaw = yaw;
        m_initialised = true;
    }
    const float yawRate = dt > 0.0f ? wrapToPi(yaw - m_yaw) / dt : 0.0f;
    m_telemetry.speed = dt > 0.0f ? glm::length(position - m_position) / dt : 0.0f;
    m_position = position;
    m_yaw = yaw;

    const glm::vec2 forward(std::sin(yaw), std::cos(yaw)); // boat +z in world xz
    const glm::vec2 port(std::cos(yaw), -std::sin(yaw)); // boat +x in world xz
    const glm::vec2 positionXZ(position.x, position.z);

    // ---- 2. Wind in the boat frame
    const float windFrom = glm::radians(toggles.windFromDeg);
    const glm::vec2 windFromWorld(std::sin(windFrom), std::cos(windFrom));
    const glm::vec2 windLocal(glm::dot(windFromWorld, port), glm::dot(windFromWorld, forward)); // x > 0: wind from port
    m_windAngle = std::acos(std::clamp(windLocal.y, -1.0f, 1.0f));
    const float windSide = windLocal.x >= 0.0f ? 1.0f : -1.0f;

    // ---- 3. Buoyancy: sample the same wave function the GPU draws at bow, stern and both sides
    float targetHeave = 0.0f, targetPitch = 0.0f, targetRoll = 0.0f;
    if (toggles.buoyancy) {
        constexpr float halfLength = 2.8f, halfBeam = 1.0f;
        const float hBow = waves.heightAt(positionXZ + forward * halfLength, time);
        const float hStern = waves.heightAt(positionXZ - forward * halfLength, time);
        const float hPort = waves.heightAt(positionXZ + port * halfBeam, time);
        const float hStarboard = waves.heightAt(positionXZ - port * halfBeam, time);
        targetHeave = 0.25f * (hBow + hStern + hPort + hStarboard);
        targetPitch = std::atan2(hStern - hBow, 2.0f * halfLength); // bow up = negative rotation about +x
        targetRoll = std::atan2(hPort - hStarboard, 2.0f * halfBeam); // port up = positive rotation about +z
    }
    smoothTowards(m_heave, targetHeave, dt, 0.3f);
    smoothTowards(m_pitch, targetPitch, dt, 0.35f);
    smoothTowards(m_roll, targetRoll, dt, 0.35f);

    // ---- 4. Heel: the wind pushes the sail to leeward, most on a beam reach
    smoothTowards(m_heel, glm::radians(22.0f) * toggles.windStrength * windLocal.x, dt, 1.0f);

    // ---- 5. Boom and sail: the boom eases out as the wind comes further aft, on the leeward side.
    // It swings at a limited rate, so tacks and gybes show as the boom crossing the boat.
    const float trim = std::clamp(0.55f * m_windAngle - 0.1f, 0.05f, 1.45f);
    const float targetBoom = windSide * trim;
    m_boomAngle += std::clamp(targetBoom - m_boomAngle, -1.2f * dt, 1.2f * dt);
    // Billow to leeward (boom-local -x when the boom is out to starboard), flat when pointing into the wind.
    const float fullness = (0.25f + 0.75f * toggles.windStrength) * glm::smoothstep(0.35f, 0.7f, m_windAngle);
    smoothTowards(m_billow, (m_boomAngle >= 0.0f ? -1.0f : 1.0f) * fullness, dt, 0.25f);
    // Never scale to exactly zero: the normal matrix (inverse transpose) would be singular.
    const float sailScaleX = std::abs(m_billow) < 0.05f ? std::copysign(0.05f, m_billow) : m_billow;

    // ---- 6. Rudder follows the turn rate (blade to port turns the boat to port)
    smoothTowards(m_rudderAngle, std::clamp(-3.0f * yawRate, -0.6f, 0.6f), dt, 0.3f);

    // ---- 7. Write the animation matrices and update the hierarchy
    m_root->anim = translate(glm::vec3(position.x, m_heave, position.z)) * rotateY(yaw) * rotateX(m_pitch) * rotateZ(m_roll + m_heel);
    m_rudder->anim = rotateY(m_rudderAngle);
    m_boom->anim = rotateY(m_boomAngle);
    m_sail->anim = glm::scale(glm::mat4(1.0f), glm::vec3(sailScaleX, 1.0f, 1.0f));
    m_vane->anim = rotateY(std::atan2(windLocal.x, windLocal.y)); // pointer (+z) towards where the wind comes from
    m_root->updateWorld();

    m_telemetry.courseParameter = m_u;
    m_telemetry.distance = m_distance;
    m_telemetry.windAngleDeg = glm::degrees(m_windAngle);
    m_telemetry.boomDeg = glm::degrees(m_boomAngle);
    m_telemetry.heelDeg = glm::degrees(m_heel);
    m_telemetry.rudderDeg = glm::degrees(m_rudderAngle);
}
