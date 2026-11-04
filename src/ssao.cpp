#include "ssao.h"
#include "mesh.h"
#include "uniforms.h"
#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <glm/gtc/matrix_inverse.hpp>
DISABLE_WARNINGS_POP()
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <utility>

namespace {
Shader makeShader(const char* vert, const char* frag)
{
    ShaderBuilder builder;
    builder.addStage(GL_VERTEX_SHADER, std::filesystem::path(RESOURCE_ROOT "shaders/") / vert);
    builder.addStage(GL_FRAGMENT_SHADER, std::filesystem::path(RESOURCE_ROOT "shaders/") / frag);
    return builder.build();
}
void setTexParameters(GLenum filter)
{
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}
}

SsaoPass::SsaoPass()
    : m_geometry(makeShader("ssao_geometry_vert.glsl", "ssao_geometry_frag.glsl"))
    , m_occlusion(makeShader("fullscreen_vert.glsl", "ssao_frag.glsl"))
    , m_blur(makeShader("fullscreen_vert.glsl", "ssao_blur_frag.glsl"))
{
    glGenFramebuffers(1, &m_gbuffer);
    glGenFramebuffers(1, &m_aoFbo);
    glGenFramebuffers(1, &m_blurFbo);
    glGenTextures(1, &m_normal);
    glGenTextures(1, &m_depth);
    glGenTextures(1, &m_aoRaw);
    glGenTextures(1, &m_aoBlurred);
}

SsaoPass::~SsaoPass()
{
    glDeleteTextures(1, &m_normal);
    glDeleteTextures(1, &m_depth);
    glDeleteTextures(1, &m_aoRaw);
    glDeleteTextures(1, &m_aoBlurred);
    glDeleteFramebuffers(1, &m_gbuffer);
    glDeleteFramebuffers(1, &m_aoFbo);
    glDeleteFramebuffers(1, &m_blurFbo);
}

void SsaoPass::resize(glm::ivec2 size)
{
    if (size == m_size || size.x <= 0 || size.y <= 0)
        return;
    m_size = size;
    const glm::ivec2 half { std::max(1, (size.x + 1) / 2), std::max(1, (size.y + 1) / 2) };
    glBindTexture(GL_TEXTURE_2D, m_normal);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, size.x, size.y, 0, GL_RGB, GL_FLOAT, nullptr);
    setTexParameters(GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, m_depth);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, size.x, size.y, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    setTexParameters(GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
    glBindFramebuffer(GL_FRAMEBUFFER, m_gbuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_normal, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_depth, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cerr << "SSAO normal/depth framebuffer is incomplete\n";
    for (const auto& [tex, fbo] : { std::pair<GLuint, GLuint> { m_aoRaw, m_aoFbo }, { m_aoBlurred, m_blurFbo } }) {
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, half.x, half.y, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
        setTexParameters(GL_LINEAR);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            std::cerr << "SSAO occlusion framebuffer is incomplete\n";
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void SsaoPass::render(const CameraView& camera, glm::ivec2 size, const std::vector<DrawItem>& items,
    float radius, float strength, GLuint fullscreenVao)
{
    resize(size);
    if (m_size.x <= 0)
        return;

    // (1) Store view-space normals and hardware depth for opaque geometry, including blended sails.
    glBindFramebuffer(GL_FRAMEBUFFER, m_gbuffer);
    glViewport(0, 0, m_size.x, m_size.y);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    m_geometry.bind();
    uniform::set("viewProjection", camera.projection * camera.view);
    for (const DrawItem& item : items) {
        uniform::set("modelMatrix", item.model);
        uniform::set("normalViewMatrix", glm::inverseTranspose(glm::mat3(camera.view * item.model)));
        uniform::set("blendWeight", item.blendWeight);
        item.mesh->draw();
    }

    // (2) Reconstruct view positions and compare a small, rotated hemisphere of samples.
    const glm::ivec2 half { std::max(1, (m_size.x + 1) / 2), std::max(1, (m_size.y + 1) / 2) };
    glBindFramebuffer(GL_FRAMEBUFFER, m_aoFbo);
    glViewport(0, 0, half.x, half.y);
    glDisable(GL_DEPTH_TEST);
    m_occlusion.bind();
    uniform::set("projection", camera.projection);
    uniform::set("inverseProjection", glm::inverse(camera.projection));
    uniform::set("radius", std::clamp(radius, 0.2f, 6.0f));
    uniform::set("strength", std::clamp(strength, 0.0f, 2.5f));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_depth);
    uniform::set("depthTexture", 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_normal);
    uniform::set("normalTexture", 1);
    glBindVertexArray(fullscreenVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    // (3) Small depth-aware blur at half resolution, upsampled with linear filtering in PBR.
    glBindFramebuffer(GL_FRAMEBUFFER, m_blurFbo);
    m_blur.bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_aoRaw);
    uniform::set("aoTexture", 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_depth);
    uniform::set("depthTexture", 1);
    uniform::set("texel", glm::vec2(1.0f / half.x, 1.0f / half.y));
    glDrawArrays(GL_TRIANGLES, 0, 3);
}
