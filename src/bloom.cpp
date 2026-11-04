#include "bloom.h"
#include "uniforms.h"
#include <algorithm>
#include <filesystem>
#include <stdexcept>

namespace {
Shader bloomShader(const char* fragmentFile)
{
    ShaderBuilder builder;
    builder.addStage(GL_VERTEX_SHADER, RESOURCE_ROOT "shaders/fullscreen_vert.glsl");
    builder.addStage(GL_FRAGMENT_SHADER, std::filesystem::path(RESOURCE_ROOT "shaders/") / fragmentFile);
    return builder.build();
}
} // namespace

BloomPass::BloomPass()
    : m_brightShader(bloomShader("bloom_bright_frag.glsl"))
    , m_blurShader(bloomShader("bloom_blur_frag.glsl"))
{
}

BloomPass::~BloomPass()
{
    release();
}

void BloomPass::release()
{
    glDeleteFramebuffers(static_cast<GLsizei>(m_fbo.size()), m_fbo.data());
    glDeleteTextures(static_cast<GLsizei>(m_color.size()), m_color.data());
    m_fbo.fill(0);
    m_color.fill(0);
    m_size = glm::ivec2(0);
}

void BloomPass::resize(glm::ivec2 framebufferSize)
{
    if (framebufferSize.x <= 0 || framebufferSize.y <= 0)
        return;

    // Round up rather than truncate, so odd framebuffer dimensions still cover the final pixel.
    const glm::ivec2 halfSize = (framebufferSize + glm::ivec2(1)) / 2;
    if (halfSize == m_size)
        return;
    release();
    m_size = halfSize;

    glGenFramebuffers(static_cast<GLsizei>(m_fbo.size()), m_fbo.data());
    glGenTextures(static_cast<GLsizei>(m_color.size()), m_color.data());
    for (size_t i = 0; i < m_color.size(); ++i) {
        glBindTexture(GL_TEXTURE_2D, m_color[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, m_size.x, m_size.y, 0, GL_RGBA, GL_FLOAT, nullptr);
        // Bilinear filtering combines pairs of Gaussian taps in the blur shader.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_color[i], 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("Bloom framebuffer is incomplete");
    }
}

void BloomPass::render(GLuint hdrColor, glm::ivec2 framebufferSize, float threshold, int iterations, GLuint fullscreenVao)
{
    if (framebufferSize.x <= 0 || framebufferSize.y <= 0)
        return;
    resize(framebufferSize);

    // Bloom is a pure full-screen post-process; disabling blending and depth testing prevents
    // the HDR scene's depth buffer or the preceding transparent passes from affecting it.
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glViewport(0, 0, m_size.x, m_size.y);
    glBindVertexArray(fullscreenVao);

    // Pass 1: sample the full-resolution HDR buffer into half-resolution bright-only texture 0.
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo[0]);
    m_brightShader.bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, hdrColor);
    uniform::set("hdrColor", 0);
    const float safeThreshold = std::max(threshold, 0.01f);
    uniform::set("threshold", safeThreshold);
    uniform::set("knee", safeThreshold * 0.5f);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    // Pass 2: alternate horizontal (texture 0 -> 1) and vertical (1 -> 0) Gaussian passes.
    // Never sample from the same texture that is attached for drawing: that is undefined in GL.
    m_blurShader.bind();
    uniform::set("source", 0);
    const glm::vec2 pixel = 1.0f / glm::vec2(m_size);
    for (int i = 0; i < std::clamp(iterations, 1, 6); ++i) {
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo[1]);
        glBindTexture(GL_TEXTURE_2D, m_color[0]);
        uniform::set("direction", glm::vec2(pixel.x, 0.0f));
        glDrawArrays(GL_TRIANGLES, 0, 3);

        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo[0]);
        glBindTexture(GL_TEXTURE_2D, m_color[1]);
        uniform::set("direction", glm::vec2(0.0f, pixel.y));
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    glEnable(GL_DEPTH_TEST);
    // Renderer::tonemap() binds the screen framebuffer and restores its full viewport.
}
