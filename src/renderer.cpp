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
#include <array>

namespace {

// Texture units used by the scene shaders.
constexpr GLint AlbedoUnit = 0;
constexpr GLint NormalUnit = 1;
constexpr GLint RoughnessUnit = 2;
constexpr GLint EnvironmentUnit = 3;
constexpr GLint ShadowUnit = 4; // only ever used by sampler2DShadow (mixing sampler types on one unit is an error)

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
    , m_shadowShader(buildShader("shadow_vert.glsl", "shadow_frag.glsl")) // the starter code's depth-only shaders
    , m_depthDebugShader(buildShader("fullscreen_vert.glsl", "depth_debug_frag.glsl"))
{
    m_shadowMap.resize(2048);

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

void Renderer::render(const FrameContext& frame, const std::vector<DrawItem>& items, const std::vector<LineBatch>& lines,
    Water& water, const Waves& waves, const Sky& sky, const FeatureToggles& toggles)
{
    if (toggles.shadows)
        drawShadowMap(frame, items, toggles);

    m_hdr.resize(frame.framebufferSize);
    m_hdr.bind();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    // No back-face culling: the sail is a single double-sided sheet and the hull is open under the deck.
    glDisable(GL_CULL_FACE);

    drawOpaque(frame, items, sky, toggles);
    drawWater(frame, water, waves, sky, toggles);
    drawLines(frame, lines);
    // Drawn last so it only covers pixels nothing else wrote; it makes no sense from straight above.
    if (toggles.skybox && !frame.camera.orthographic)
        drawSkybox(frame, sky);

    tonemap(frame, toggles);
    if (toggles.shadows && toggles.showShadowMap)
        drawShadowMapDebugView(frame);
}

void Renderer::drawShadowMap(const FrameContext& frame, const std::vector<DrawItem>& items, const FeatureToggles& toggles)
{
    m_shadowMap.resize(toggles.shadowResolution);
    m_lightSpace = directionalLightMatrix(frame.lighting.lightDirection, frame.shadowCenter, toggles.shadowExtent, ShadowDepthRange, m_shadowMap.resolution());

    m_shadowMap.bind();
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE); // the sail is a single sheet, both sides must cast
    // Push the stored depth back a little, more on slopes, so surfaces do not shadow themselves (acne).
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.5f, 2.0f);

    // Only the boat and buoys cast shadows. The water only receives: letting the waves cast onto
    // themselves would need much more bias and gives little visible gain.
    m_shadowShader.bind();
    for (const DrawItem& item : items) {
        uniform::set("mvpMatrix", m_lightSpace * item.model);
        item.mesh->draw();
    }
    glDisable(GL_POLYGON_OFFSET_FILL);
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
        uniform::set("doubleSided", m.doubleSided);
        uniform::set("uvScale", m.uvScale);

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

void Renderer::tonemap(const FrameContext& frame, const FeatureToggles& toggles)
{
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, frame.framebufferSize.x, frame.framebufferSize.y);
    glDisable(GL_DEPTH_TEST);
    m_postShader.bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_hdr.colorTexture());
    uniform::set("hdrColor", 0);
    uniform::set("exposure", toggles.exposure);
    glBindVertexArray(m_emptyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glEnable(GL_DEPTH_TEST);
}
