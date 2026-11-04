#include "flag.h"
#include "procedural_mesh.h"
#include "procedural_texture.h"
#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat3x3.hpp>
DISABLE_WARNINGS_POP()
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace {

// Staffs and cloths, in metres. The staffs stand on the quarters of the transom, in the Stern node's
// frame (+x port, +y up, +z forward, origin on the waterline in the middle of the transom).
constexpr glm::vec3 StaffFoot { 0.55f, 0.5f, 0.12f }; // port staff (x mirrored for starboard); a little below the deck edge (~0.64 m up), so no gap shows
constexpr float StaffHeight = 1.5f;
constexpr float ClothWidth = 0.9f; // along the fly, away from the staff
constexpr float ClothHeight = 0.6f; // along the hoist, on the staff
constexpr float ClothWaveDepth = 0.05f; // fixed ripple in the mesh, so the cloth is not a flat card

// Flipbook atlas: Columns x Rows cells of 128 x 88 texels. Each holds one 120 x 80 frame (3:2, like
// the cloth) inside a 4-texel gutter that repeats the frame's edge.
constexpr int CellWidth = 128;
constexpr int CellHeight = 88;
constexpr int Gutter = 4;
constexpr glm::vec2 CellSize { static_cast<float>(CellWidth), static_cast<float>(CellHeight) };
constexpr glm::vec2 AtlasSize { static_cast<float>(Flag::Columns * CellWidth), static_cast<float>(Flag::Rows * CellHeight) };

// The two flags: design, staff side (+1 = port, -1 = starboard), node names, and how many frames the
// flipbook runs ahead of the first flag.
struct FlagSpec {
    FlagDesign design;
    float side;
    const char* staffName;
    const char* clothName;
    int frameLag;
};
constexpr std::array<FlagSpec, 2> FlagSpecs { {
    { FlagDesign::Hungary, 1.0f, "PortStaff", "HungarianFlag", 0 },
    { FlagDesign::SouthAfrica, -1.0f, "StarboardStaff", "SouthAfricanFlag", 5 },
} };

glm::mat4 translate(const glm::vec3& t) { return glm::translate(glm::mat4(1.0f), t); }
glm::mat4 rotateY(float a) { return glm::rotate(glm::mat4(1.0f), a, glm::vec3(0, 1, 0)); }

// Lower-left corner of a frame's picture in the atlas, in texture coordinates (past the gutter).
glm::vec2 frameOffset(int frame)
{
    const glm::vec2 cell(static_cast<float>(frame % Flag::Columns), static_cast<float>(frame / Flag::Columns));
    return (cell * CellSize + glm::vec2(static_cast<float>(Gutter))) / AtlasSize;
}

// The cloth: hoist along +y on the staff (x = z = 0), fly along -z. Texture coordinate u runs along the
// fly from the staff (0) to the free end (1), v from the bottom (0) to the top (1).
Mesh makeCloth()
{
    constexpr int columns = 24, rows = 12;
    Mesh mesh;
    mesh.material.kd = glm::vec3(1.0f); // unused by the PBR shader, but GPUMesh copies it (like newMesh() does)
    for (int j = 0; j <= rows; ++j) {
        for (int i = 0; i <= columns; ++i) {
            const float u = static_cast<float>(i) / static_cast<float>(columns);
            const float v = static_cast<float>(j) / static_cast<float>(rows);
            // A wave across the fly, growing away from the staff that holds the cloth.
            const float x = ClothWaveDepth * u * std::sin(glm::two_pi<float>() * 1.5f * u);
            mesh.vertices.push_back(Vertex { glm::vec3(x, v * ClothHeight, -u * ClothWidth), glm::vec3(0.0f), glm::vec2(u, v) });
        }
    }
    const auto stride = static_cast<uint32_t>(columns + 1);
    for (uint32_t j = 0; j < static_cast<uint32_t>(rows); ++j) {
        for (uint32_t i = 0; i < static_cast<uint32_t>(columns); ++i) {
            const uint32_t a = j * stride + i;
            mesh.triangles.emplace_back(a, a + 1, a + stride + 1);
            mesh.triangles.emplace_back(a, a + stride + 1, a + stride);
        }
    }
    computeSmoothNormals(mesh);
    return mesh;
}

}


Flag::Flag()
{
    m_staffMaterial = { .albedo = glm::vec3(0.45f, 0.2f, 0.09f), .roughness = 0.25f }; // varnished, like the tiller

    // ---- Meshes, shared by both flags
    Mesh ball = makeSphere(0.03f, 12, 6);
    transformMesh(ball, translate(glm::vec3(0.0f, StaffHeight, 0.0f)));
    const std::array<Mesh, 2> staffParts { makeCylinder(0.02f, 0.014f, StaffHeight, 12), ball };
    m_meshes.push_back(std::make_unique<GPUMesh>(mergeMeshList(staffParts)));
    GPUMesh* staffMesh = m_meshes.back().get();
    m_meshes.push_back(std::make_unique<GPUMesh>(makeCloth()));
    GPUMesh* clothMesh = m_meshes.back().get();

    // ---- Per flag: atlas, material, staff and cloth. One frame of the atlas (without its gutter) covers
    // the cloth's whole [0, 1]^2 UV square, so uvScale stays fixed; update() only moves uvOffset.
    static_assert(FlagSpecs.size() == 2, "one FlagSpec per Cloth in flag.h");
    m_root = std::make_unique<SceneNode>("Flags");
    for (size_t k = 0; k < m_cloths.size(); ++k) {
        const FlagSpec& spec = FlagSpecs[k];
        const ImageRGBA atlas = generateFlagAtlas(spec.design, Columns, Rows, CellWidth, CellHeight, Gutter);
        m_atlases.push_back(std::make_unique<Texture>(atlas.width, atlas.height, atlas.pixels, true));

        Cloth& cloth = m_cloths[k];
        cloth.material = {
            .albedo = glm::vec3(1.0f),
            .roughness = 0.85f,
            .albedoMap = m_atlases.back().get(),
            .uvScale = (CellSize - glm::vec2(static_cast<float>(2 * Gutter))) / AtlasSize,
            .uvOffset = frameOffset(spec.frameLag),
            .doubleSided = true,
        };
        cloth.frameLag = spec.frameLag;
        SceneNode& staff = m_root->addChild(spec.staffName, staffMesh, &m_staffMaterial, translate(glm::vec3(spec.side * StaffFoot.x, StaffFoot.y, StaffFoot.z)));
        // The top of the cloth sits just below the ball on the staff.
        cloth.node = &staff.addChild(spec.clothName, clothMesh, &cloth.material, translate(glm::vec3(0.0f, StaffHeight - ClothHeight - 0.04f, 0.0f)));
    }
}

void Flag::update(float dt, const FeatureToggles& toggles, const glm::mat4& sternWorld)
{
    m_root->visible = toggles.flag;

    // ---- 1. Flipbook. The frame follows simulation time, so the cloth ripples equally fast at 30 or
    // 144 fps. The rate is integrated (phase += fps * dt) instead of computed as time * fps, so moving
    // the slider, or the wind changing the rate, never makes the animation jump.
    if (toggles.flagFreeze) {
        m_frame = std::clamp(toggles.flagFrame, 0, FrameCount - 1);
    } else {
        const float fps = toggles.flagFps * (0.25f + 0.75f * toggles.windStrength); // flutters faster in a stronger wind
        m_phase = std::fmod(m_phase + fps * dt, static_cast<float>(FrameCount));
        m_frame = static_cast<int>(m_phase) % FrameCount;
    }
    for (Cloth& cloth : m_cloths)
        cloth.material.uvOffset = frameOffset((m_frame + cloth.frameLag) % FrameCount);

    // ---- 2. Stream downwind. The wind direction goes into the boat frame (the Stern node's rotation
    // is orthonormal, so its inverse is its transpose) and is smoothed, so the flag swings round
    // instead of snapping when the boat turns or the wind slider moves.
    const float windFrom = glm::radians(toggles.windFromDeg);
    const glm::vec3 downwind = glm::transpose(glm::mat3(sternWorld)) * -glm::vec3(std::sin(windFrom), 0.0f, std::cos(windFrom));
    const glm::vec2 target(downwind.x, downwind.z);
    if (glm::length(target) > 1e-4f)
        m_downwind += (glm::normalize(target) - m_downwind) * (1.0f - std::exp(-dt / 0.6f));
    // The fly is the cloth's -z: turn each cloth about its staff (y) until it points downwind.
    const float angle = std::atan2(-m_downwind.x, -m_downwind.y);
    for (Cloth& cloth : m_cloths)
        cloth.node->anim = rotateY(angle);

    m_root->updateWorld(sternWorld);
}
