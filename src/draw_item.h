#pragma once
#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
DISABLE_WARNINGS_POP()
#include <vector>

class GPUMesh;
class Texture;

// Metallic/roughness material. Texture pointers are optional; a null map means "use the factor only".
struct PBRMaterial {
    glm::vec3 albedo { 0.8f };
    float roughness { 0.5f };
    float metallic { 0.0f };
    glm::vec3 emissive { 0.0f };

    Texture* albedoMap { nullptr }; // sRGB, multiplied with albedo
    Texture* normalMap { nullptr }; // tangent space
    Texture* roughnessMap { nullptr }; // red channel, multiplied with roughness
    glm::vec2 uvScale { 1.0f };

    bool doubleSided { false }; // thin surfaces such as the sail
};

// One mesh to draw with the PBR shader: produced by SceneNode::collect, consumed by the Renderer.
struct DrawItem {
    GPUMesh* mesh;
    const PBRMaterial* material;
    glm::mat4 model;
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
