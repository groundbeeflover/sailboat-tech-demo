#pragma once
#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
DISABLE_WARNINGS_POP()
#include <functional>
#include <vector>

class GPUMesh;
class Texture;
struct FrameContext;

// Metallic/roughness material. Texture pointers are optional; a null map means "use the factor only".
struct PBRMaterial {
    glm::vec3 albedo { 0.8f };
    float roughness { 0.5f };
    float metallic { 0.0f };
    glm::vec3 emissive { 0.0f };

    Texture* albedoMap { nullptr }; // sRGB, multiplied with albedo
    Texture* normalMap { nullptr }; // tangent space
    Texture* roughnessMap { nullptr }; // red channel, multiplied with roughness
    // Texture coordinates used for the maps: uv * uvScale + uvOffset. Animating the offset/scale
    // from scene code selects frames of a flipbook atlas (animated textures) without renderer changes.
    glm::vec2 uvScale { 1.0f };
    glm::vec2 uvOffset { 0.0f };

    bool doubleSided { false }; // thin surfaces such as the sail
};

// One mesh to draw with the PBR shader: produced by SceneNode::collect, consumed by the Renderer.
struct DrawItem {
    GPUMesh* mesh;
    const PBRMaterial* material;
    glm::mat4 model;
    // Blend-shape weight: the vertex shaders add blendWeight * (delta position/normal) from vertex
    // attributes 4 and 5. Meshes without those attributes are unaffected by it.
    float blendWeight { 0.0f };
};

// Debug geometry (Bezier curve, control polygon). Kept free of OpenGL types so the simulation
// side can produce it.
enum class LineMode { Strip, Loop, Segments, Points };

struct LineBatch {
    std::vector<glm::vec3> points;
    LineMode mode { LineMode::Strip };
    glm::vec3 color { 1.0f };
    float pointSize { 1.0f };
};

// A custom draw callback that runs inside the scene pass (see Renderer::render for the exact GL state).
// Lets a feature (particles, light beams, ...) keep all its drawing code in its own files.
using ExtraPass = std::function<void(const FrameContext&)>;

// Everything the scene hands to the renderer for one frame. New kinds of content become a new
// field here instead of a new Renderer::render parameter.
struct RenderLists {
    std::vector<DrawItem> opaque; // PBR meshes: cast and receive shadows
    std::vector<LineBatch> lines; // debug lines and points (Bezier curve, markers)
    std::vector<ExtraPass> transparent; // blended passes after the water, before the skybox
};
