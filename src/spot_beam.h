#pragma once
#include "lights.h"
#include <framework/opengl_includes.h>
#include <framework/shader.h>

struct FrameContext;

// Cheap view-dependent volumetric *impression*: several translucent cone shells, not ray marching.
// Drawn through RenderLists::transparent, with depth testing and additive blending.
class SpotBeam {
public:
    SpotBeam();
    ~SpotBeam();
    SpotBeam(const SpotBeam&) = delete;
    SpotBeam& operator=(const SpotBeam&) = delete;
    void draw(const FrameContext& frame, float opacity);
private:
    Shader m_shader;
    GLuint m_vao { 0 };
    GLuint m_vbo { 0 };
    GLsizei m_vertices { 0 };
};
