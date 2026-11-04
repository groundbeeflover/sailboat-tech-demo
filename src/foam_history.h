#pragma once
#include <framework/opengl_includes.h>
#include <framework/shader.h>
#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <glm/vec2.hpp>
DISABLE_WARNINGS_POP()

class Waves;

// A world-space history of the instantaneous wave-compression mask, ping-ponged every frame.
// Unlike a screen-space effect, moving the camera does not smear foam across the boat or island.
class FoamHistory {
public:
    FoamHistory();
    ~FoamHistory();
    FoamHistory(const FoamHistory&) = delete;
    FoamHistory& operator=(const FoamHistory&) = delete;

    void update(const Waves& waves, float time, float dt, glm::vec2 center, float threshold, float lifetime,
        GLuint fullscreenVao);
    void reset() { m_valid = false; }
    GLuint texture() const { return m_texture[m_front]; }
    glm::vec2 center() const { return m_center; }
    float span() const { return WorldSpan; }
    bool valid() const { return m_valid; }

private:
    static constexpr int Size = 512;
    static constexpr float WorldSpan = 230.0f;
    Shader m_shader;
    GLuint m_fbo[2] { 0, 0 };
    GLuint m_texture[2] { 0, 0 };
    int m_front { 0 };
    bool m_valid { false };
    glm::vec2 m_center { 0.0f };
};
