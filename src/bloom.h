#pragma once
#include <framework/disable_all_warnings.h>
#include <framework/opengl_includes.h>
#include <framework/shader.h>
DISABLE_WARNINGS_PUSH()
#include <glm/vec2.hpp>
DISABLE_WARNINGS_POP()
#include <array>

// B1: Extract HDR highlights and blur them before tone mapping. This pass owns only its own
// half-resolution color targets; the scene HDR target and depth buffer remain unchanged.
class BloomPass {
public:
    BloomPass();
    BloomPass(const BloomPass&) = delete;
    BloomPass& operator=(const BloomPass&) = delete;
    ~BloomPass();

    // Must be called after the scene (including transparent extras and skybox) is complete.
    // Leaves the last vertical blur in m_color[0], ready for the post-processing composite.
    // The caller subsequently binds the default framebuffer and restores the full viewport.
    void render(GLuint hdrColor, glm::ivec2 framebufferSize, float threshold, int iterations, GLuint fullscreenVao);
    GLuint texture() const { return m_color[0]; }

private:
    void resize(glm::ivec2 framebufferSize);
    void release();

    Shader m_brightShader;
    Shader m_blurShader;
    std::array<GLuint, 2> m_fbo {};
    std::array<GLuint, 2> m_color {};
    glm::ivec2 m_size { 0 };
};
