#include "sky.h"
#include "uniforms.h"
#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
DISABLE_WARNINGS_POP()
#include <cmath>

Lighting computeLighting(float hours)
{
    // The sun rises at 6:00 and sets at 18:00, on a tilted arc that peaks at about 65 degrees.
    const float phi = (hours - 6.0f) / 12.0f * glm::pi<float>();
    Lighting l;
    l.sunDirection = glm::normalize(glm::vec3(-std::cos(phi), 0.9f * std::sin(phi), 0.45f));
    const float elevation = l.sunDirection.y;

    // Keep these in line with the sun colour in sky_frag.glsl.
    const glm::vec3 sunColor = glm::mix(glm::vec3(1.0f, 0.5f, 0.25f), glm::vec3(1.0f, 0.95f, 0.88f), glm::smoothstep(0.0f, 0.35f, elevation));
    const float sunIntensity = 3.2f * glm::smoothstep(-0.02f, 0.08f, elevation);
    if (elevation > -0.02f) {
        l.lightDirection = l.sunDirection;
        l.lightColor = sunColor * sunIntensity;
    } else {
        // Moonlight: dim and bluish, from the opposite side of the sky.
        l.lightDirection = -l.sunDirection;
        l.lightColor = glm::vec3(0.35f, 0.45f, 0.7f) * 0.25f * glm::smoothstep(0.0f, 0.1f, -elevation);
    }
    const float day = glm::smoothstep(-0.12f, 0.15f, elevation);
    l.ambientColor = glm::mix(glm::vec3(0.02f, 0.03f, 0.06f), glm::vec3(0.35f, 0.45f, 0.6f), day) * 0.6f;
    return l;
}

Sky::Sky(int faceSize)
    : m_faceSize(faceSize)
    , m_maxLod(std::floor(std::log2(static_cast<float>(faceSize))))
{
    ShaderBuilder builder;
    builder.addStage(GL_VERTEX_SHADER, RESOURCE_ROOT "shaders/fullscreen_vert.glsl");
    builder.addStage(GL_FRAGMENT_SHADER, RESOURCE_ROOT "shaders/sky_frag.glsl");
    m_shader = builder.build();

    // Half-float faces so the sun can be much brighter than 1 (HDR).
    glGenTextures(1, &m_cubemap);
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_cubemap);
    for (GLenum face = 0; face < 6; ++face)
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_RGBA16F, faceSize, faceSize, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
    // Filter across face edges, otherwise the blurry mips used for rough reflections show seams.
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

    glGenFramebuffers(1, &m_fbo);
    glGenVertexArrays(1, &m_emptyVao);
}

Sky::~Sky()
{
    glDeleteVertexArrays(1, &m_emptyVao);
    glDeleteFramebuffers(1, &m_fbo);
    glDeleteTextures(1, &m_cubemap);
}

void Sky::update(const glm::vec3& sunDirection)
{
    if (m_valid && glm::length(sunDirection - m_lastSun) < 1e-4f)
        return;
    m_valid = true;
    m_lastSun = sunDirection;

    // Per face: the direction through its centre and the directions of increasing s and t,
    // following the cubemap face layout of the OpenGL specification (table 8.19).
    struct Face {
        glm::vec3 forward, right, up;
    };
    const Face faces[6] = {
        { { 1, 0, 0 }, { 0, 0, -1 }, { 0, -1, 0 } }, // +X
        { { -1, 0, 0 }, { 0, 0, 1 }, { 0, -1, 0 } }, // -X
        { { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } }, // +Y
        { { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, -1 } }, // -Y
        { { 0, 0, 1 }, { 1, 0, 0 }, { 0, -1, 0 } }, // +Z
        { { 0, 0, -1 }, { -1, 0, 0 }, { 0, -1, 0 } }, // -Z
    };

    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, m_faceSize, m_faceSize);
    glDisable(GL_DEPTH_TEST);
    m_shader.bind();
    uniform::set("sunDirection", sunDirection);
    uniform::set("faceSize", static_cast<float>(m_faceSize));
    glBindVertexArray(m_emptyVao);
    for (GLenum i = 0; i < 6; ++i) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, m_cubemap, 0);
        uniform::set("faceForward", faces[i].forward);
        uniform::set("faceRight", faces[i].right);
        uniform::set("faceUp", faces[i].up);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // The mip chain doubles as a cheap blur for rough reflections and diffuse ambient light.
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_cubemap);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
}
