#pragma once
#include <framework/disable_all_warnings.h>
#include <framework/opengl_includes.h>
#include <framework/shader.h>
DISABLE_WARNINGS_PUSH()
#include <glm/vec3.hpp>
DISABLE_WARNINGS_POP()

// Sun, moon and light colour for a time of day (hours, 0..24).
struct Lighting {
    glm::vec3 sunDirection { 0.0f, 1.0f, 0.0f }; // towards the sun (also below the horizon)
    glm::vec3 lightDirection { 0.0f, 1.0f, 0.0f }; // towards the light used for shading: sun by day, moon at night
    glm::vec3 lightColor { 1.0f };
    glm::vec3 ambientColor { 0.1f }; // flat ambient for when the environment map is switched off
};
Lighting computeLighting(float hours);

// Procedural sky rendered into a cubemap. That cubemap is the environment map for the skybox, the
// water reflections and the PBR image-based light. It is only re-rendered when the sun moves.
class Sky {
public:
    explicit Sky(int faceSize = 512);
    Sky(const Sky&) = delete;
    Sky& operator=(const Sky&) = delete;
    ~Sky();

    // Changes the framebuffer binding and viewport, so call it before the frame's render passes.
    void update(const glm::vec3& sunDirection);

    GLuint cubemap() const { return m_cubemap; }
    float maxLod() const { return m_maxLod; }

private:
    Shader m_shader;
    GLuint m_cubemap { 0 };
    GLuint m_fbo { 0 };
    GLuint m_emptyVao { 0 };
    int m_faceSize;
    float m_maxLod;
    glm::vec3 m_lastSun { 0.0f };
    bool m_valid { false };
};
