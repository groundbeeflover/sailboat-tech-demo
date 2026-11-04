#include "renderer.h"
#include "mesh.h"
#include "texture.h"
#include "uniforms.h"
#include "water.h"
#include "waves.h"
#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <glm/gtc/matrix_inverse.hpp>
DISABLE_WARNINGS_POP()
#include <algorithm>
#include <array>
#include <cmath>

namespace {

// Texture units used by the scene shaders.
constexpr GLint AlbedoUnit = 0;
constexpr GLint NormalUnit = 1;
constexpr GLint RoughnessUnit = 2;
constexpr GLint EnvironmentUnit = 3;
constexpr GLint ShadowUnit = 4; // only ever used by sampler2DShadow (mixing sampler types on one unit is an error)
constexpr GLint WideShadowUnit = 6;
constexpr GLint SsaoUnit = 7;
constexpr GLint FoamHistoryUnit = 8;
constexpr GLint SpotShadowUnit = 5; // second independent sampler2DShadow
constexpr int SpotShadowResolution = 1024;
constexpr float SpotNearPlane = 0.5f;
constexpr float SunCascadeSplit = 55.0f; // world metres along the perspective camera's forward vector
constexpr float SunCascadeOverlap = 12.0f;
constexpr float SunCascadeFar = 370.0f; // sufficient for the buoy course, shoreline and lighthouse

Shader buildShader(const char* vertexFile, const char* fragmentFile)
{
    ShaderBuilder builder;
    builder.addStage(GL_VERTEX_SHADER, std::filesystem::path(RESOURCE_ROOT "shaders/") / vertexFile);
    builder.addStage(GL_FRAGMENT_SHADER, std::filesystem::path(RESOURCE_ROOT "shaders/") / fragmentFile);
    return builder.build();
}

}

Renderer::Renderer()
    : m_pbrShader(buildShader("pbr_vert.glsl", "pbr_frag.glsl"))
    , m_waterShader(buildShader("water_vert.glsl", "water_frag.glsl"))
    , m_skyboxShader(buildShader("skybox_vert.glsl", "skybox_frag.glsl"))
    , m_lineShader(buildShader("line_vert.glsl", "line_frag.glsl"))
    , m_postShader(buildShader("fullscreen_vert.glsl", "post_frag.glsl"))
    , m_minimapOverlayShader(buildShader("fullscreen_vert.glsl", "minimap_overlay_frag.glsl"))
    , m_shadowShader(buildShader("shadow_vert.glsl", "shadow_frag.glsl")) // the starter code's depth-only shaders
    , m_depthDebugShader(buildShader("fullscreen_vert.glsl", "depth_debug_frag.glsl"))
    , m_spotDepthDebugShader(buildShader("fullscreen_vert.glsl", "spot_depth_debug_frag.glsl"))
    , m_ssaoDebugShader(buildShader("fullscreen_vert.glsl", "ssao_debug_frag.glsl"))
{
    m_shadowMap.resize(2048);
    m_spotShadowMap.resize(SpotShadowResolution);
    m_minimap.resize(256); // create the stable ImGui texture ID before the first UI frame

    // Core profile needs some VAO bound even for draws that generate their vertices from gl_VertexID.
    glGenVertexArrays(1, &m_emptyVao);

    glGenVertexArrays(1, &m_lineVao);
    glBindVertexArray(m_lineVao);
    glGenBuffers(1, &m_lineVbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_lineVbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);
    glBindVertexArray(0);

    glEnable(GL_PROGRAM_POINT_SIZE);
}

Renderer::~Renderer()
{
    glDeleteBuffers(1, &m_lineVbo);
    glDeleteVertexArrays(1, &m_lineVao);
    glDeleteVertexArrays(1, &m_emptyVao);
}

void Renderer::render(const FrameContext& frame, const RenderLists& lists, Water& water, const Waves& waves, const Sky& sky, const FeatureToggles& toggles)
{
    m_currentCascades = toggles.shadows && toggles.cascadedShadows;
    if (toggles.shadows)
        drawShadowMap(frame, lists.opaque, toggles);
    // Independent second depth pass, only at night while the island's lamp is enabled.
    if (toggles.spotlight && frame.spot.enabled && toggles.spotShadows)
        drawSpotShadowMap(frame, lists.opaque);

    if (toggles.foam && toggles.temporalFoam) {
        const float dt = m_previousFoamTime < 0.0f ? 0.0f : std::max(frame.time - m_previousFoamTime, 0.0f);
        m_foamHistory.update(waves, frame.time, dt, glm::vec2(frame.shadowCenter.x, frame.shadowCenter.z),
            toggles.foamThreshold, toggles.foamLifetime, m_emptyVao);
    } else {
        m_foamHistory.reset();
    }
    m_previousFoamTime = frame.time;

    if (toggles.ssao)
        m_ssao.render(frame.camera, frame.framebufferSize, lists.opaque,
            toggles.ssaoRadius, toggles.ssaoStrength, m_emptyVao);

    m_hdr.resize(frame.framebufferSize);
    m_hdr.bind();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    // No back-face culling: the sail is a single double-sided sheet and the hull is open under the deck.
    glDisable(GL_CULL_FACE);

    drawOpaque(frame, lists.opaque, sky, toggles);
    drawWater(frame, water, waves, sky, toggles);
    drawLines(frame, lists.lines);
    // The sky must be behind transparent particles / the lighthouse beam. Since blended passes
    // do not write depth, drawing the sky afterwards would erase their pixels over the horizon.
    if (toggles.skybox && !frame.camera.orthographic)
        drawSkybox(frame, sky);
    drawTransparent(frame, lists.transparent);

    if (toggles.bloom)
        m_bloom.render(m_hdr.colorTexture(), frame.framebufferSize, toggles.bloomThreshold, toggles.bloomIterations, m_emptyVao);

    tonemap(m_hdr.colorTexture(), toggles.bloom ? m_bloom.texture() : m_hdr.colorTexture(),
        0, frame.framebufferSize, toggles.bloom, toggles.showBloomBuffer, toggles.bloomIntensity, toggles.exposure);
    if (toggles.ssao && toggles.showSsao) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, frame.framebufferSize.x, frame.framebufferSize.y);
        glDisable(GL_DEPTH_TEST);
        m_ssaoDebugShader.bind();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_ssao.texture());
        uniform::set("aoTexture", 0);
        glBindVertexArray(m_emptyVao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glEnable(GL_DEPTH_TEST);
    }
    if (toggles.shadows && toggles.showShadowMap)
        drawShadowMapDebugView(frame);
    if (toggles.spotlight && frame.spot.enabled && toggles.spotShadows && toggles.showSpotShadowMap)
        drawSpotShadowMapDebugView(frame);
}

void Renderer::renderMinimap(const FrameContext& frame, const RenderLists& lists, Water& water,
    const Waves& waves, const Sky& sky, const FeatureToggles& toggles, const MinimapInputs& inputs)
{
    if (!toggles.minimap)
        return;

    m_minimap.resize(toggles.minimapSize);
    FrameContext mapFrame = frame;
    mapFrame.camera = Minimap::camera(inputs, toggles.minimapFollowsHeading);
    mapFrame.framebufferSize = glm::ivec2(m_minimap.side());

    // Reuse the geometry/lighting passes, but not the expensive shadow-map construction, bloom,
    // skybox or particle effects. These do not improve a navigation map at this small resolution.
    FeatureToggles mapToggles = toggles;
    mapToggles.shadows = false;
    mapToggles.spotShadows = false;
    mapToggles.fog = false;
    mapToggles.waterWireframe = false;
    mapToggles.ssao = false; // AO texture belongs to the main camera, not the minimap camera.

    m_minimap.bindHdr();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);
    drawOpaque(mapFrame, lists.opaque, sky, mapToggles);
    drawWater(mapFrame, water, waves, sky, mapToggles);
    drawLines(mapFrame, lists.lines);

    // A course-scale orthographic camera makes the boat only a few pixels wide. Draw a simple
    // forward-pointing triangle above the scene so its position and heading remain readable.
    const glm::vec3 forward(std::sin(inputs.boatYaw), 0.0f, std::cos(inputs.boatYaw));
    const glm::vec3 right(std::cos(inputs.boatYaw), 0.0f, -std::sin(inputs.boatYaw));
    const glm::vec3 origin = inputs.boatPosition + glm::vec3(0.0f, 27.0f, 0.0f);
    LineBatch arrow;
    arrow.mode = LineMode::Loop;
    arrow.color = glm::vec3(3.0f, 0.7f, 0.15f);
    arrow.points = { origin + forward * 8.0f,
        origin - forward * 5.0f + right * 4.0f,
        origin - forward * 5.0f - right * 4.0f };
    LineBatch center;
    center.mode = LineMode::Points;
    center.color = glm::vec3(3.0f, 0.7f, 0.15f);
    center.pointSize = 9.0f;
    center.points = { origin };
    drawLines(mapFrame, { arrow, center });

    // Exactly the same ACES + gamma output transform as the main scene, but *no* bloom.
    // The LDR RGBA8 texture is suitable for ImGui::Image without further colour conversion.
    tonemap(m_minimap.hdrTexture(), m_minimap.hdrTexture(), m_minimap.displayFramebuffer(),
        mapFrame.framebufferSize, false, false, 0.0f, toggles.exposure);

    // The default framebuffer + main viewport must be active for ImGui and for screenshot capture.
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, frame.framebufferSize.x, frame.framebufferSize.y);
}

void Renderer::drawMinimapCaptureOverlay(glm::ivec2 framebufferSize)
{
    // The normal interactive map lives in ImGui's movable window. Scripted captures read the
    // scene *before* ImGui gets rendered, so show the identical already-tonemapped map as a
    // top-right picture-in-picture here. The extra draw is only used by --capture and P shots.
    const int padding = 16;
    const int side = std::min(m_minimap.side(), std::min(framebufferSize.x, framebufferSize.y) - 2 * padding - 4);
    if (side <= 0)
        return;
    const int x = framebufferSize.x - side - padding;
    const int y = framebufferSize.y - side - padding;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_DEPTH_TEST);
    // Narrow dark frame separates the water-coloured map from the main ocean view.
    glEnable(GL_SCISSOR_TEST);
    glScissor(x - 3, y - 3, side + 6, side + 6);
    glClearColor(0.04f, 0.05f, 0.07f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);

    glViewport(x, y, side, side);
    m_minimapOverlayShader.bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_minimap.texture());
    uniform::set("minimapColor", 0);
    glBindVertexArray(m_emptyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glViewport(0, 0, framebufferSize.x, framebufferSize.y);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::drawShadowMap(const FrameContext& frame, const std::vector<DrawItem>& items, const FeatureToggles& toggles)
{
    m_shadowMap.resize(toggles.shadowResolution);
    const bool perspectiveCascades = toggles.cascadedShadows && !frame.camera.orthographic;
    m_lightSpace = perspectiveCascades
        ? directionalCascadeMatrix(frame.camera, frame.lighting.lightDirection,
            Cameras::NearPlane, SunCascadeSplit + SunCascadeOverlap, m_shadowMap.resolution())
        : directionalLightMatrix(frame.lighting.lightDirection, frame.shadowCenter,
            toggles.shadowExtent, ShadowDepthRange, m_shadowMap.resolution());

    m_shadowMap.bind();
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE); // the sail is a single sheet, both sides must cast
    // Push the stored depth back a little, more on slopes, so surfaces do not shadow themselves (acne).
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.5f, 2.0f);

    // The water receives but does not cast shadows; blend-shaped sails cast correctly.
    renderDepthItems(m_lightSpace, items);
    if (toggles.cascadedShadows) {
        // Perspective views: true camera-depth frustum slices, 55m split and 12m overlap.
        // Orthographic top view: depth slices are meaningless (camera is 500m overhead),
        // so fall back to two anchored spatial levels around the boat and whole course.
        m_shadowMapWide.resize(toggles.shadowResolution);
        m_lightSpaceWide = perspectiveCascades
            ? directionalCascadeMatrix(frame.camera, frame.lighting.lightDirection,
                SunCascadeSplit - SunCascadeOverlap, SunCascadeFar, m_shadowMapWide.resolution())
            : directionalLightMatrix(frame.lighting.lightDirection,
                frame.wideShadowCenter, frame.wideShadowExtent,
                ShadowDepthRange * 4.0f, m_shadowMapWide.resolution());
        m_shadowMapWide.bind();
        glClear(GL_DEPTH_BUFFER_BIT);
        renderDepthItems(m_lightSpaceWide, items);
    }
    glDisable(GL_POLYGON_OFFSET_FILL);
}

void Renderer::renderDepthItems(const glm::mat4& lightSpace, const std::vector<DrawItem>& items, bool spotlightPass)
{
    m_shadowShader.bind();
    for (const DrawItem& item : items) {
        if (spotlightPass && !item.castsSpotShadow)
            continue;
        uniform::set("mvpMatrix", lightSpace * item.model);
        uniform::set("blendWeight", item.blendWeight);
        item.mesh->draw();
    }
}

void Renderer::drawSpotShadowMap(const FrameContext& frame, const std::vector<DrawItem>& items)
{
    m_spotLightSpace = spotLightMatrix(frame.spot, SpotNearPlane);
    m_spotShadowMap.bind();
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE); // a single-sheet sail casts on both sides
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.5f, 2.0f);
    // All opaque meshes except the emitting lamp itself cast into this map.
    // The terrain, lantern structure, boat, and moving sail still occlude the beam.
    renderDepthItems(m_spotLightSpace, items, true);
    glDisable(GL_POLYGON_OFFSET_FILL);
}

void Renderer::drawSpotShadowMapDebugView(const FrameContext& frame)
{
    const GLsizei size = frame.framebufferSize.y / 4;
    // Leave room for both sun cascades when the two-map debug view is enabled.
    glViewport(12 + (m_currentCascades ? 2 : 1) * (size + 12), 12, size, size);
    glDisable(GL_DEPTH_TEST);
    m_spotDepthDebugShader.bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_spotShadowMap.depthTexture());
    // For debugging sampler2D raw depth, disable the shadow comparison temporarily.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
    uniform::set("depthTexture", 0);
    uniform::set("nearPlane", SpotNearPlane);
    uniform::set("farPlane", frame.spot.range);
    glBindVertexArray(m_emptyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glViewport(0, 0, frame.framebufferSize.x, frame.framebufferSize.y);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::drawShadowMapDebugView(const FrameContext& frame)
{
    // Bottom-left corner of the screen, a quarter of the window height.
    const GLsizei size = frame.framebufferSize.y / 4;
    glViewport(12, 12, size, size);
    glDisable(GL_DEPTH_TEST);
    m_depthDebugShader.bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_shadowMap.depthTexture());
    // A plain sampler2D only reads raw depth values with comparison switched off.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
    uniform::set("depthTexture", 0);
    glBindVertexArray(m_emptyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    if (m_shadowMapWide.resolution() && m_shadowMapWide.depthTexture() && m_currentCascades) {
        glViewport(24 + size, 12, size, size);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_shadowMapWide.depthTexture());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    }
    glViewport(0, 0, frame.framebufferSize.x, frame.framebufferSize.y);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::setFrameUniforms(const FrameContext& frame, const Sky& sky, const FeatureToggles& toggles) const
{
    uniform::set("viewProjection", frame.camera.projection * frame.camera.view);
    uniform::set("cameraPosition", frame.camera.position);
    uniform::set("orthographic", frame.camera.orthographic);
    uniform::set("viewForward", frame.camera.forward);
    uniform::set("lightDirection", frame.lighting.lightDirection);
    uniform::set("lightColor", frame.lighting.lightColor);
    uniform::set("ambientColor", frame.lighting.ambientColor);
    uniform::set("useEnvironmentMap", toggles.environmentMap);
    uniform::set("environmentMaxLod", sky.maxLod());
    // Distance fog is meaningless for the orthographic map view.
    uniform::set("useFog", toggles.fog && !frame.camera.orthographic);
    uniform::set("fogDistance", FogDistance);

    glActiveTexture(GL_TEXTURE0 + EnvironmentUnit);
    glBindTexture(GL_TEXTURE_CUBE_MAP, sky.cubemap());
    uniform::set("environmentMap", EnvironmentUnit);

    uniform::set("useShadows", toggles.shadows);
    uniform::set("lightSpaceMatrix", m_lightSpace);
    uniform::set("pcfRadius", toggles.pcfRadius);
    uniform::set("shadowBias", toggles.shadowBias);
    glActiveTexture(GL_TEXTURE0 + ShadowUnit);
    glBindTexture(GL_TEXTURE_2D, m_shadowMap.depthTexture());
    uniform::set("shadowMap", ShadowUnit);
    uniform::set("useCascades", toggles.shadows && toggles.cascadedShadows);
    uniform::set("lightSpaceMatrixWide", m_lightSpaceWide);
    uniform::set("cascadeSplit", SunCascadeSplit);
    uniform::set("cascadeOverlap", SunCascadeOverlap);
    glActiveTexture(GL_TEXTURE0 + WideShadowUnit);
    glBindTexture(GL_TEXTURE_2D, toggles.shadows && toggles.cascadedShadows
        ? m_shadowMapWide.depthTexture() : m_shadowMap.depthTexture());
    uniform::set("shadowMapWide", WideShadowUnit);

    uniform::set("useSsao", toggles.ssao);
    uniform::set("ssaoViewport", glm::vec2(frame.framebufferSize));
    glActiveTexture(GL_TEXTURE0 + SsaoUnit);
    glBindTexture(GL_TEXTURE_2D, toggles.ssao ? m_ssao.texture() : 0);
    uniform::set("ssaoTexture", SsaoUnit);

    const bool activeSpot = toggles.spotlight && frame.spot.enabled;
    uniform::set("useSpotlight", activeSpot);
    uniform::set("spotPosition", frame.spot.position);
    uniform::set("spotDirection", frame.spot.direction);
    uniform::set("spotColor", frame.spot.color);
    uniform::set("spotCosInner", std::cos(frame.spot.innerAngle));
    uniform::set("spotCosOuter", std::cos(frame.spot.outerAngle));
    uniform::set("spotRange", frame.spot.range);
    uniform::set("useSpotShadows", activeSpot && toggles.spotShadows);
    uniform::set("spotLightSpaceMatrix", m_spotLightSpace);
    uniform::set("spotBias", toggles.spotBias);
    glActiveTexture(GL_TEXTURE0 + SpotShadowUnit);
    glBindTexture(GL_TEXTURE_2D, m_spotShadowMap.depthTexture());
    uniform::set("spotShadowMap", SpotShadowUnit);
}

void Renderer::drawOpaque(const FrameContext& frame, const std::vector<DrawItem>& items, const Sky& sky, const FeatureToggles& toggles)
{
    m_pbrShader.bind();
    setFrameUniforms(frame, sky, toggles);
    uniform::set("usePBR", toggles.pbr);
    uniform::set("albedoMap", AlbedoUnit);
    uniform::set("normalMap", NormalUnit);
    uniform::set("roughnessMap", RoughnessUnit);

    for (const DrawItem& item : items) {
        const PBRMaterial& m = *item.material;
        uniform::set("modelMatrix", item.model);
        // Inverse transpose keeps normals perpendicular under non-uniform scale (the sail).
        uniform::set("normalMatrix", glm::inverseTranspose(glm::mat3(item.model)));

        uniform::set("albedoFactor", m.albedo);
        uniform::set("roughnessFactor", m.roughness);
        uniform::set("metallicFactor", m.metallic);
        uniform::set("emissive", m.emissive);
        uniform::set("transmission", toggles.sailTranslucency ? m.transmission * toggles.sailTransmission : 0.0f);
        uniform::set("doubleSided", m.doubleSided);
        uniform::set("uvScale", m.uvScale);
        uniform::set("uvOffset", m.uvOffset);
        uniform::set("blendWeight", item.blendWeight);

        const bool albedoMap = toggles.textures && m.albedoMap;
        const bool roughnessMap = toggles.textures && m.roughnessMap;
        const bool normalMap = toggles.normalMapping && m.normalMap;
        uniform::set("hasAlbedoMap", albedoMap);
        uniform::set("hasRoughnessMap", roughnessMap);
        uniform::set("hasNormalMap", normalMap);
        if (albedoMap)
            m.albedoMap->bind(GL_TEXTURE0 + AlbedoUnit);
        if (normalMap)
            m.normalMap->bind(GL_TEXTURE0 + NormalUnit);
        if (roughnessMap)
            m.roughnessMap->bind(GL_TEXTURE0 + RoughnessUnit);

        item.mesh->draw();
    }
}

void Renderer::drawWater(const FrameContext& frame, Water& water, const Waves& waves, const Sky& sky, const FeatureToggles& toggles)
{
    m_waterShader.bind();
    setFrameUniforms(frame, sky, toggles);

    // Pack the wave set into two uniform arrays (see water_vert.glsl).
    std::array<glm::vec4, Waves::MaxWaves> dirAmpK {};
    std::array<glm::vec3, Waves::MaxWaves> omegaPhaseQ {};
    for (int i = 0; i < waves.count(); ++i) {
        const GerstnerWave& w = waves.waves()[static_cast<size_t>(i)];
        dirAmpK[static_cast<size_t>(i)] = glm::vec4(w.direction, w.amplitude, w.wavenumber);
        omegaPhaseQ[static_cast<size_t>(i)] = glm::vec3(w.omega, w.phase, w.steepness);
    }
    uniform::set("waveCount", waves.count());
    uniform::setArray("waveDirAmpK", dirAmpK.data(), Waves::MaxWaves);
    uniform::setArray("waveOmegaPhaseQ", omegaPhaseQ.data(), Waves::MaxWaves);
    uniform::set("time", frame.time);
    uniform::set("gridOffset", water.gridOffset(frame.camera.position));
    uniform::set("windDirection", waves.mainDirection());
    uniform::set("useFoam", toggles.foam);
    uniform::set("foamThreshold", std::clamp(toggles.foamThreshold, 0.55f, 0.99f));
    const bool history = toggles.foam && toggles.temporalFoam && m_foamHistory.valid();
    uniform::set("useFoamHistory", history);
    uniform::set("foamHistoryCenter", m_foamHistory.center());
    uniform::set("foamHistorySpan", m_foamHistory.span());
    glActiveTexture(GL_TEXTURE0 + FoamHistoryUnit);
    glBindTexture(GL_TEXTURE_2D, history ? m_foamHistory.texture() : 0);
    uniform::set("foamHistoryTexture", FoamHistoryUnit);

    uniform::set("useDetailNormals", toggles.waterDetailNormals);
    water.detailNormals().bind(GL_TEXTURE0);
    uniform::set("detailNormalMap", 0);

    if (toggles.waterWireframe)
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    water.draw();
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

void Renderer::drawLines(const FrameContext& frame, const std::vector<LineBatch>& lines)
{
    if (lines.empty())
        return;
    m_lineShader.bind();
    uniform::set("viewProjection", frame.camera.projection * frame.camera.view);
    glBindVertexArray(m_lineVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_lineVbo);
    for (const LineBatch& batch : lines) {
        if (batch.points.empty())
            continue;
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(batch.points.size() * sizeof(glm::vec3)), batch.points.data(), GL_DYNAMIC_DRAW);
        uniform::set("color", batch.color);
        uniform::set("pointSize", batch.pointSize);
        uniform::set("roundPoints", batch.mode == LineMode::Points);
        GLenum mode = GL_LINE_STRIP;
        switch (batch.mode) {
        case LineMode::Strip:
            mode = GL_LINE_STRIP;
            break;
        case LineMode::Loop:
            mode = GL_LINE_LOOP;
            break;
        case LineMode::Segments:
            mode = GL_LINES;
            break;
        case LineMode::Points:
            mode = GL_POINTS;
            break;
        }
        glDrawArrays(mode, 0, static_cast<GLsizei>(batch.points.size()));
    }
}

void Renderer::drawTransparent(const FrameContext& frame, const std::vector<ExtraPass>& passes)
{
    if (passes.empty())
        return;
    // Blended geometry is tested against the opaque depth but must not write depth itself,
    // otherwise overlapping transparent surfaces would hide each other.
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    for (const ExtraPass& pass : passes)
        pass(frame);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ZERO);
}

void Renderer::drawSkybox(const FrameContext& frame, const Sky& sky)
{
    m_skyboxShader.bind();
    // Rotation only: the sky is infinitely far away, so camera translation must not move it.
    const glm::mat4 rotationOnly = glm::mat4(glm::mat3(frame.camera.view));
    uniform::set("inverseViewProjection", glm::inverse(frame.camera.projection * rotationOnly));
    glActiveTexture(GL_TEXTURE0 + EnvironmentUnit);
    glBindTexture(GL_TEXTURE_CUBE_MAP, sky.cubemap());
    uniform::set("environmentMap", EnvironmentUnit);

    glDepthFunc(GL_LEQUAL); // the triangle sits exactly on the far plane (depth 1)
    glDepthMask(GL_FALSE);
    glBindVertexArray(m_emptyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
}

void Renderer::tonemap(GLuint hdrColor, GLuint bloomColor, GLuint destinationFbo,
    glm::ivec2 destinationSize, bool useBloom, bool showBloomBuffer, float bloomIntensity, float exposure)
{
    glBindFramebuffer(GL_FRAMEBUFFER, destinationFbo);
    glViewport(0, 0, destinationSize.x, destinationSize.y);
    glDisable(GL_DEPTH_TEST);
    m_postShader.bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, hdrColor);
    uniform::set("hdrColor", 0);
    // Keep unit 1 separate from the shadow-map comparison sampler used by scene passes.
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, bloomColor);
    uniform::set("bloomColor", 1);
    uniform::set("useBloom", useBloom);
    uniform::set("showBloomBuffer", useBloom && showBloomBuffer);
    uniform::set("bloomIntensity", bloomIntensity);
    uniform::set("exposure", exposure);
    glBindVertexArray(m_emptyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glEnable(GL_DEPTH_TEST);
}
