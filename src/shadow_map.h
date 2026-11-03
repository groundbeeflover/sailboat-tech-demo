#pragma once
#include <framework/disable_all_warnings.h>
#include <framework/opengl_includes.h>
DISABLE_WARNINGS_PUSH()
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
DISABLE_WARNINGS_POP()

// Depth-only render target seen from the sun (or moon).
// The depth texture has hardware comparison enabled, so shaders sample it through a
// sampler2DShadow: each lookup returns the fraction of lit samples, bilinearly filtered.
class ShadowMap {
public:
    ShadowMap() = default;
    ShadowMap(const ShadowMap&) = delete;
    ShadowMap& operator=(const ShadowMap&) = delete;
    ~ShadowMap();

    // (Re)allocates the depth texture when the resolution changes.
    void resize(int resolution);
    void bind() const; // binds the framebuffer and sets the viewport

    GLuint depthTexture() const { return m_depth; }
    int resolution() const { return m_resolution; }

private:
    void release();

    GLuint m_fbo { 0 };
    GLuint m_depth { 0 };
    int m_resolution { 0 };
};

// Orthographic light-space (projection * view) matrix for a directional light.
// The box is centred on `center`, covers +-halfExtent metres perpendicular to the light and
// +-depthRange metres along it. Its centre is snapped to whole shadow-map texels, so shadow edges
// do not crawl ("shimmer") while the boat moves.
glm::mat4 directionalLightMatrix(const glm::vec3& lightDirection, const glm::vec3& center, float halfExtent, float depthRange, int resolution);
