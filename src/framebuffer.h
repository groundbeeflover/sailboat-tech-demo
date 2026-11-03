#pragma once
#include <framework/disable_all_warnings.h>
#include <framework/opengl_includes.h>
DISABLE_WARNINGS_PUSH()
#include <glm/vec2.hpp>
DISABLE_WARNINGS_POP()

// Off-screen render target with a half-float colour texture (HDR: values above 1 survive until
// tone mapping, which bloom needs) and a depth buffer.
class HdrTarget {
public:
    HdrTarget() = default;
    HdrTarget(const HdrTarget&) = delete;
    HdrTarget& operator=(const HdrTarget&) = delete;
    ~HdrTarget();

    // (Re)allocates the attachments when the size changes.
    void resize(const glm::ivec2& size);
    void bind() const; // binds the framebuffer and sets the viewport

    GLuint colorTexture() const { return m_color; }
    glm::ivec2 size() const { return m_size; }

private:
    void release();

    GLuint m_fbo { 0 };
    GLuint m_color { 0 };
    GLuint m_depth { 0 };
    glm::ivec2 m_size { 0 };
};
