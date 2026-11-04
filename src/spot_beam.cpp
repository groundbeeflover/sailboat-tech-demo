#include "spot_beam.h"
#include "renderer.h"
#include "uniforms.h"
#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
DISABLE_WARNINGS_POP()
#include <algorithm>
#include <cstddef>
#include <cmath>
#include <filesystem>
#include <vector>

namespace {
Shader makeBeamShader()
{
    ShaderBuilder builder;
    builder.addStage(GL_VERTEX_SHADER, std::filesystem::path(RESOURCE_ROOT "shaders/beam_vert.glsl"));
    builder.addStage(GL_FRAGMENT_SHADER, std::filesystem::path(RESOURCE_ROOT "shaders/beam_frag.glsl"));
    return builder.build();
}
struct BeamVertex { glm::vec3 position; float shell; };
}

SpotBeam::SpotBeam() : m_shader(makeBeamShader())
{
    // The nested surfaces suggest scattering throughout the light volume without a 3D texture.
    // Geometry is stored in normalised cone coordinates and reused as the lamp rotates.
    constexpr int segments = 32;
    constexpr int slices = 18;
    std::vector<BeamVertex> vertices;
    for (float shell : { 0.37f, 0.68f, 1.0f }) {
        for (int s = 0; s < slices; ++s) {
            const float t0 = (static_cast<float>(s) + 0.02f) / slices;
            const float t1 = (static_cast<float>(s + 1) + 0.02f) / slices;
            for (int j = 0; j < segments; ++j) {
                const float a = glm::two_pi<float>() * static_cast<float>(j) / segments;
                const float b = glm::two_pi<float>() * static_cast<float>(j + 1) / segments;
                auto point = [shell](float t, float angle) {
                    return BeamVertex { { shell * t * std::cos(angle), shell * t * std::sin(angle), t }, shell };
                };
                const auto p00 = point(t0, a), p10 = point(t1, a);
                const auto p01 = point(t0, b), p11 = point(t1, b);
                vertices.insert(vertices.end(), { p00, p10, p01, p01, p10, p11 });
            }
        }
    }
    m_vertices = static_cast<GLsizei>(vertices.size());
    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);
    glGenBuffers(1, &m_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(BeamVertex)), vertices.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(BeamVertex), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(BeamVertex), reinterpret_cast<void*>(offsetof(BeamVertex, shell)));
    glBindVertexArray(0);
}

SpotBeam::~SpotBeam()
{
    glDeleteBuffers(1, &m_vbo);
    glDeleteVertexArrays(1, &m_vao);
}

void SpotBeam::draw(const FrameContext& frame, float opacity)
{
    if (!frame.spot.enabled || opacity <= 0.0f)
        return;
    const glm::vec3 axis = glm::normalize(frame.spot.direction);
    const glm::vec3 ref = std::abs(glm::dot(axis, glm::vec3(0, 1, 0))) > 0.95f
        ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
    const glm::vec3 right = glm::normalize(glm::cross(ref, axis));
    const glm::vec3 up = glm::cross(axis, right);
    glm::mat4 beamToWorld(1.0f);
    beamToWorld[0] = glm::vec4(right, 0.0f);
    beamToWorld[1] = glm::vec4(up, 0.0f);
    beamToWorld[2] = glm::vec4(axis, 0.0f);
    beamToWorld[3] = glm::vec4(frame.spot.position, 1.0f);

    const float reach = std::min(frame.spot.range, 145.0f);
    m_shader.bind();
    uniform::set("viewProjection", frame.camera.projection * frame.camera.view);
    uniform::set("beamToWorld", beamToWorld);
    uniform::set("beamLength", reach);
    uniform::set("beamRadius", std::tan(frame.spot.outerAngle) * reach);
    // The actual spotlight intensity is ~20,000 cd and is inverse-square. The beam is an
    // artistic scattering approximation and deliberately uses normalised warm colours.
    const glm::vec3 tint = frame.spot.color / std::max({ frame.spot.color.r, frame.spot.color.g, frame.spot.color.b, 1.0f });
    uniform::set("beamColor", tint * 2.1f);
    uniform::set("beamOpacity", std::clamp(opacity, 0.0f, 1.0f));
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, m_vertices);
    // ExtraPass accepts the altered blend function; renderer restores the default afterwards.
}
