#pragma once
#include "bloom.h"
#include "camera.h"
#include "draw_item.h"
#include "feature_toggles.h"
#include "framebuffer.h"
#include "lights.h"
#include "shadow_map.h"
#include "sky.h"
#include <framework/disable_all_warnings.h>
#include <framework/opengl_includes.h>
#include <framework/shader.h>
DISABLE_WARNINGS_PUSH()
#include <glm/vec2.hpp>
DISABLE_WARNINGS_POP()
#include <vector>

class Water;
class Waves;

// Per-frame inputs that are the same for every pass.
struct FrameContext {
    CameraView camera;
    Lighting lighting;
    float time { 0.0f };
    glm::ivec2 framebufferSize { 1 };
    glm::vec3 shadowCenter { 0.0f }; // the shadow map covers an area around this point (the boat)
    SpotLight spot; // second light (e.g. the lighthouse lamp); disabled until the scene provides one
};

// Owns the shaders and the render targets, and draws one frame:
//   two shadow maps: orthographic sun/moon + optional perspective lighthouse lamp
//   HDR target: opaque PBR meshes -> water -> debug lines -> extra transparent passes -> skybox
//   screen:     tone mapping + gamma, optional shadow-map view (ImGui is drawn on top afterwards by the Window)
class Renderer {
public:
    Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    ~Renderer();

    // Contract for the ExtraPass callbacks in lists.transparent: they run with the HDR target bound,
    // depth test on (GL_LESS) but depth writes off, and blending on with
    // glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA). A pass may bind its own shader, VAO and
    // textures and change the blend function (e.g. additive); the renderer restores blending and depth
    // writes afterwards. A pass must not change the framebuffer or the viewport.
    void render(const FrameContext& frame, const RenderLists& lists, Water& water, const Waves& waves, const Sky& sky, const FeatureToggles& toggles);

    static constexpr float FogDistance = 260.0f; // metres at which fog reaches ~63%
    static constexpr float ShadowDepthRange = 30.0f; // metres in front of and behind the shadow centre

private:
    void drawShadowMap(const FrameContext& frame, const std::vector<DrawItem>& items, const FeatureToggles& toggles);
    void drawShadowMapDebugView(const FrameContext& frame);
    void drawSpotShadowMap(const FrameContext& frame, const std::vector<DrawItem>& items);
    void drawSpotShadowMapDebugView(const FrameContext& frame);
    void renderDepthItems(const glm::mat4& lightSpace, const std::vector<DrawItem>& items, bool spotlightPass = false);
    void setFrameUniforms(const FrameContext& frame, const Sky& sky, const FeatureToggles& toggles) const;
    void drawOpaque(const FrameContext& frame, const std::vector<DrawItem>& items, const Sky& sky, const FeatureToggles& toggles);
    void drawWater(const FrameContext& frame, Water& water, const Waves& waves, const Sky& sky, const FeatureToggles& toggles);
    void drawLines(const FrameContext& frame, const std::vector<LineBatch>& lines);
    void drawTransparent(const FrameContext& frame, const std::vector<ExtraPass>& passes);
    void drawSkybox(const FrameContext& frame, const Sky& sky);
    void tonemap(const FrameContext& frame, const FeatureToggles& toggles);

    Shader m_pbrShader;
    Shader m_waterShader;
    Shader m_skyboxShader;
    Shader m_lineShader;
    Shader m_postShader;
    Shader m_shadowShader;
    Shader m_depthDebugShader;
    Shader m_spotDepthDebugShader;

    HdrTarget m_hdr;
    BloomPass m_bloom; // optional half-res HDR highlights, composited before ACES
    ShadowMap m_shadowMap;
    ShadowMap m_spotShadowMap; // fixed 1024x1024 depth-only FBO
    glm::mat4 m_lightSpace { 1.0f };
    glm::mat4 m_spotLightSpace { 1.0f };
    GLuint m_emptyVao { 0 };
    GLuint m_lineVao { 0 };
    GLuint m_lineVbo { 0 };
};
