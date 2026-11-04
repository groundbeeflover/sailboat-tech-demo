#include "foam_history.h"
#include "waves.h"
#include "uniforms.h"
#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
DISABLE_WARNINGS_POP()
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>

namespace {
Shader makeHistoryShader()
{
    ShaderBuilder builder;
    builder.addStage(GL_VERTEX_SHADER, std::filesystem::path(RESOURCE_ROOT "shaders/fullscreen_vert.glsl"));
    builder.addStage(GL_FRAGMENT_SHADER, std::filesystem::path(RESOURCE_ROOT "shaders/foam_history_frag.glsl"));
    return builder.build();
}
}

FoamHistory::FoamHistory() : m_shader(makeHistoryShader())
{
    glGenTextures(2, m_texture);
    glGenFramebuffers(2, m_fbo);
    for (int i = 0; i < 2; ++i) {
        glBindTexture(GL_TEXTURE_2D, m_texture[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, Size, Size, 0, GL_RED, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_texture[i], 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            std::cerr << "Foam history framebuffer is incomplete\n";
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

FoamHistory::~FoamHistory()
{
    glDeleteFramebuffers(2, m_fbo);
    glDeleteTextures(2, m_texture);
}

void FoamHistory::update(const Waves& waves, float time, float dt, glm::vec2 center,
    float threshold, float lifetime, GLuint fullscreenVao)
{
    if (waves.count() == 0) {
        reset(); // calm water must not retain old whitecaps
        return;
    }
    const bool havePrevious = m_valid && glm::distance(center, m_center) < WorldSpan * 0.4f;
    const int next = 1 - m_front;
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo[next]);
    glViewport(0, 0, Size, Size);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    m_shader.bind();
    std::array<glm::vec4, Waves::MaxWaves> dirAmpK {};
    std::array<glm::vec3, Waves::MaxWaves> omegaPhaseQ {};
    for (int i = 0; i < waves.count(); ++i) {
        const auto& w = waves.waves()[static_cast<size_t>(i)];
        dirAmpK[static_cast<size_t>(i)] = glm::vec4(w.direction, w.amplitude, w.wavenumber);
        omegaPhaseQ[static_cast<size_t>(i)] = glm::vec3(w.omega, w.phase, w.steepness);
    }
    uniform::set("waveCount", waves.count());
    uniform::setArray("waveDirAmpK", dirAmpK.data(), Waves::MaxWaves);
    uniform::setArray("waveOmegaPhaseQ", omegaPhaseQ.data(), Waves::MaxWaves);
    uniform::set("time", time);
    uniform::set("foamThreshold", std::clamp(threshold, 0.55f, 0.99f));
    uniform::set("newCenter", center);
    uniform::set("oldCenter", m_center);
    uniform::set("worldSpan", WorldSpan);
    uniform::set("decay", std::exp(-std::clamp(dt, 0.0f, 0.1f) / std::max(lifetime, 0.05f)));
    uniform::set("havePrevious", havePrevious);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_texture[m_front]);
    uniform::set("previousFoam", 0);
    glBindVertexArray(fullscreenVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    m_front = next;
    m_center = center;
    m_valid = true;
}
