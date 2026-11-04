#pragma once
#include "camera.h"
#include "framebuffer.h"
#include <framework/disable_all_warnings.h>
#include <framework/opengl_includes.h>
DISABLE_WARNINGS_PUSH()
#include <glm/vec3.hpp>
DISABLE_WARNINGS_POP()

// Navigation data supplied by the simulation without making the minimap depend on Boat or Course.
struct MinimapInputs {
    glm::vec3 boatPosition { 0.0f };
    float boatYaw { 0.0f };
    glm::vec3 courseCenter { 0.0f };
    float courseRadius { 100.0f };
};

// B3: a second HDR scene target and a display-ready RGBA8 colour texture. The renderer draws the
// same scene from an orthographic top-down camera, then tone maps into the RGBA8 FBO for ImGui.
// Keeping the LDR texture name stable across resizes is important: ImGui may already have recorded
// the texture ID earlier in the frame, before the render passes resize the attachment.
class Minimap {
public:
    Minimap() = default;
    Minimap(const Minimap&) = delete;
    Minimap& operator=(const Minimap&) = delete;
    ~Minimap();

    void resize(int side);
    void bindHdr() const { m_hdr.bind(); }
    void bindLdr() const;
    GLuint hdrTexture() const { return m_hdr.colorTexture(); }
    GLuint texture() const { return m_color; }
    GLuint displayFramebuffer() const { return m_fbo; }
    int side() const { return m_side; }

    // No separate Cameras instance: updating the chase camera here would change its smoothing.
    static CameraView camera(const MinimapInputs& inputs, bool headingUp);

private:
    HdrTarget m_hdr;
    GLuint m_fbo { 0 };
    GLuint m_color { 0 };
    int m_side { 0 };
};
