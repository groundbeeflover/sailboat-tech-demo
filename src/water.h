#pragma once
#include "texture.h"
#include <framework/disable_all_warnings.h>
#include <framework/opengl_includes.h>
DISABLE_WARNINGS_PUSH()
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
DISABLE_WARNINGS_POP()
#include <memory>

// The "endless" ocean: one fixed grid that is re-centred under the camera every frame.
// Vertex spacing grows quadratically with distance (fine near the camera, coarse far away),
// and the grid only moves in steps of the finest spacing, so near vertices land exactly on
// old vertex positions and the surface does not visibly swim. Fog hides the outer edge.
class Water {
public:
    Water(int resolution = 400, float halfSize = 450.0f);
    Water(const Water&) = delete;
    Water& operator=(const Water&) = delete;
    ~Water();

    // Offset to pass to water_vert.glsl for a camera at cameraPosition.
    glm::vec2 gridOffset(const glm::vec3& cameraPosition) const;
    float halfSize() const { return m_halfSize; }

    void draw() const;
    Texture& detailNormals() { return *m_detailNormals; }

private:
    GLuint m_vao { 0 };
    GLuint m_vbo { 0 };
    GLuint m_ibo { 0 };
    GLsizei m_indexCount { 0 };
    float m_halfSize;
    float m_snapStep;
    std::unique_ptr<Texture> m_detailNormals;
};
