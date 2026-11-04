#pragma once
#include "camera.h"
#include "draw_item.h"
#include <framework/opengl_includes.h>
#include <framework/shader.h>
#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <glm/vec2.hpp>
DISABLE_WARNINGS_POP()
#include <vector>

// Half-resolution SSAO based on an opaque depth/normal prepass. Only ambient PBR lighting
// is modulated (not sun, lighthouse, or emission); water and transparent spray are excluded.
class SsaoPass {
public:
    SsaoPass();
    ~SsaoPass();
    SsaoPass(const SsaoPass&) = delete;
    SsaoPass& operator=(const SsaoPass&) = delete;
    void render(const CameraView& camera, glm::ivec2 size, const std::vector<DrawItem>& items,
        float radius, float strength, GLuint fullscreenVao);
    GLuint texture() const { return m_aoBlurred; }

private:
    void resize(glm::ivec2 size);
    Shader m_geometry;
    Shader m_occlusion;
    Shader m_blur;
    GLuint m_gbuffer { 0 };
    GLuint m_normal { 0 };
    GLuint m_depth { 0 };
    GLuint m_aoFbo { 0 };
    GLuint m_aoRaw { 0 };
    GLuint m_blurFbo { 0 };
    GLuint m_aoBlurred { 0 };
    glm::ivec2 m_size { 0 };
};
