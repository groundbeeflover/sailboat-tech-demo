#pragma once
#include "draw_item.h"
#include "feature_toggles.h"
#include "mesh.h"
#include "scene_node.h"
#include "texture.h"
#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
DISABLE_WARNINGS_POP()
#include <array>
#include <memory>
#include <vector>

// Two flags on staffs at the boat's stern, Hungarian on the port quarter and South African on the
// starboard quarter, each with an animated texture: a flipbook atlas of 4 x 4 frames of the cloth
// rippling, stepped through at a fixed rate in simulation time. The frame is chosen purely by
// PBRMaterial::uvScale/uvOffset, which the PBR shader applies to the texture coordinates.
//
//   Flags (no mesh; the Stern node's frame)
//   ├─ PortStaff (rest: on the port quarter of the transom)
//   │  └─ HungarianFlag (anim: Ry, streams downwind)
//   └─ StarboardStaff (rest: on the starboard quarter)
//      └─ SouthAfricanFlag (anim: Ry, streams downwind)
//
// The tree is attached to the boat through its parent matrix: update() takes the Stern node's world
// matrix and passes it to SceneNode::updateWorld(), so the flags follow heave, pitch, roll and heel.
//
// The constructor needs an OpenGL context (it uploads meshes and the atlases); update() is pure math.
class Flag {
public:
    static constexpr int Columns = 4; // flipbook atlas layout
    static constexpr int Rows = 4;
    static constexpr int FrameCount = Columns * Rows;

    Flag();

    // Advances the flipbooks, turns the flags downwind and recomputes the world matrices.
    void update(float dt, const FeatureToggles& toggles, const glm::mat4& sternWorld);

    SceneNode& root() { return *m_root; }
    int frame() const { return m_frame; } // atlas frame of the first (Hungarian) flag, 0 .. FrameCount - 1

private:
    // One flag on its staff. Its atlas texture lives in m_atlases; the cloth node points at `material`.
    struct Cloth {
        PBRMaterial material;
        SceneNode* node { nullptr };
        int frameLag { 0 }; // frames ahead of the first flag, so the two don't flutter in step
    };

    std::vector<std::unique_ptr<GPUMesh>> m_meshes;
    std::vector<std::unique_ptr<Texture>> m_atlases;
    std::array<Cloth, 2> m_cloths;
    PBRMaterial m_staffMaterial;

    std::unique_ptr<SceneNode> m_root;

    float m_phase { 0.0f }; // position in the flipbook, in frames; integrated, so fps changes don't jump
    int m_frame { 0 };
    glm::vec2 m_downwind { 0.0f, -1.0f }; // smoothed wind direction in the boat frame (x = port, y = forward)
};
