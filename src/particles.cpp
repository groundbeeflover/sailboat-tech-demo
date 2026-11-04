#include "particles.h"
#include "procedural_texture.h"
#include "renderer.h"
#include "uniforms.h"
#include "waves.h"
#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/vec2.hpp>
DISABLE_WARNINGS_POP()
#include <algorithm>
#include <array>
#include <cmath>

namespace {

constexpr float Gravity = 9.81f; // m/s^2
constexpr float MaxWindSpeed = 8.0f; // m/s of air at wind strength 1 (about 16 knots)
constexpr float SprayDrag = 1.5f; // 1/s: how fast a droplet takes on the air's velocity
constexpr float FoamDrag = 2.0f; // 1/s: how fast foam comes to rest on the water
constexpr float ReferenceSpeed = 6.0f; // m/s, the default boat speed: spray rate factor 1

}


ParticleSystem::ParticleSystem()
{
    m_particles.resize(MaxParticles);
    m_instances.reserve(MaxParticles);

    ShaderBuilder builder;
    builder.addStage(GL_VERTEX_SHADER, RESOURCE_ROOT "shaders/particle_vert.glsl");
    builder.addStage(GL_FRAGMENT_SHADER, RESOURCE_ROOT "shaders/particle_frag.glsl");
    m_shader = builder.build();

    const ImageRGBA puff = generateSprayPuff(64);
    m_puff = std::make_unique<Texture>(puff.width, puff.height, puff.pixels, false);

    // One quad as a triangle strip, corners in [-1, 1]^2. Every particle reuses these 4 vertices.
    const std::array<glm::vec2, 4> corners { glm::vec2(-1.0f, -1.0f), glm::vec2(1.0f, -1.0f), glm::vec2(-1.0f, 1.0f), glm::vec2(1.0f, 1.0f) };
    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);
    glGenBuffers(1, &m_quadVbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_quadVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(corners), corners.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(glm::vec2), nullptr);

    // Per-instance data. Divisor 1 = the attribute advances once per instance (particle) instead of
    // once per vertex, so all 4 corners of a quad read the same particle.
    glGenBuffers(1, &m_instanceVbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVbo);
    glBufferData(GL_ARRAY_BUFFER, MaxParticles * sizeof(Instance), nullptr, GL_STREAM_DRAW);
    glEnableVertexAttribArray(1); // position.xyz, size
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Instance), nullptr);
    glVertexAttribDivisor(1, 1);
    glEnableVertexAttribArray(2); // alpha, rotation, flat
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Instance), reinterpret_cast<void*>(offsetof(Instance, alpha)));
    glVertexAttribDivisor(2, 1);
    glBindVertexArray(0);
}

ParticleSystem::~ParticleSystem()
{
    glDeleteBuffers(1, &m_instanceVbo);
    glDeleteBuffers(1, &m_quadVbo);
    glDeleteVertexArrays(1, &m_vao);
}


float ParticleSystem::randomRange(float lo, float hi)
{
    return std::uniform_real_distribution<float>(lo, hi)(m_rng);
}

void ParticleSystem::update(float dt, float time, const FeatureToggles& toggles, const SprayEmitters& emitters, const Waves& waves)
{
    if (!toggles.particles) {
        m_count = 0; // switching off clears the sea at once
        m_initialised = false;
        return;
    }

    // ---- 1. Emitter velocities, measured from the motion of the marker nodes (like Boat's speed telemetry).
    const glm::vec3 bow(emitters.bow[3]);
    const glm::vec3 stern(emitters.stern[3]);
    if (!m_initialised) {
        m_previousBow = bow;
        m_previousStern = stern;
        m_initialised = true;
    }
    const glm::vec3 bowVelocity = dt > 0.0f ? (bow - m_previousBow) / dt : glm::vec3(0.0f);
    const glm::vec3 sternVelocity = dt > 0.0f ? (stern - m_previousStern) / dt : glm::vec3(0.0f);
    m_previousBow = bow;
    m_previousStern = stern;

    // ---- 2. Spawn. Rates are per second, so the amount of spray does not depend on the frame rate;
    // the fraction of a particle left over is carried to the next frame.
    const float speedFactor = std::clamp(emitters.boatSpeed / ReferenceSpeed, 0.0f, 1.5f);
    // Wave impact: a crest is higher than the bow (immersion), or the bow drops fast into a trough (slam).
    const float immersion = std::max(waves.heightAt(glm::vec2(bow.x, bow.z), time) - bow.y, 0.0f);
    const float slam = std::max(-bowVelocity.y, 0.0f);
    const float impact = std::min(6.0f * immersion + 2.5f * slam, 3.0f);

    m_sprayCredit += toggles.particleRate * speedFactor * (0.5f + impact) * dt;
    for (; m_sprayCredit >= 1.0f; m_sprayCredit -= 1.0f)
        spawnSpray(emitters.bow, bowVelocity, speedFactor * (1.0f + impact));
    m_wakeCredit += 0.1f * toggles.particleRate * speedFactor * dt;
    for (; m_wakeCredit >= 1.0f; m_wakeCredit -= 1.0f)
        spawnWake(emitters.stern, sternVelocity);

    // ---- 3. Move, age and kill
    const float windFrom = glm::radians(toggles.windFromDeg);
    // The air moves away from where the wind comes from.
    const glm::vec3 air = -MaxWindSpeed * toggles.windStrength * glm::vec3(std::sin(windFrom), 0.0f, std::cos(windFrom));
    const float sprayBlend = 1.0f - std::exp(-SprayDrag * dt); // exponential approach: frame-rate independent
    const float foamBlend = 1.0f - std::exp(-FoamDrag * dt);

    for (size_t i = 0; i < m_count;) {
        Particle& p = m_particles[i];
        p.age += dt;
        const float surface = waves.heightAt(glm::vec2(p.position.x, p.position.z), time);

        if (p.kind == Kind::Spray) {
            // Semi-implicit Euler: update the velocity first, then move with the new velocity.
            p.velocity.y -= Gravity * dt;
            p.velocity += (air - p.velocity) * sprayBlend; // drag pulls the droplet towards the air's velocity
            p.position += p.velocity * dt;
        } else {
            // Foam comes to rest on the water, nudged a little by the wind, and rides the waves.
            p.velocity += (0.05f * air - p.velocity) * foamBlend;
            p.position += p.velocity * dt;
            p.position.y = surface + 0.08f; // just above the surface, so it doesn't z-fight with the water
        }

        // Dead when too old, or when a droplet fell back into the sea. The water is drawn before us with
        // depth writes on, so whatever sinks below the surface is already hidden: removing it a bit
        // lower never pops.
        if (p.age >= p.life || p.position.y < surface - 0.3f) {
            p = m_particles[--m_count]; // swap-remove: move the last live particle into this slot
            continue; // and look at slot i again
        }

        // Size and opacity for draw(): grow, fade in quickly, fade out over the second half of the life.
        const float t = p.age / p.life;
        p.size = glm::mix(p.startSize, p.endSize, t);
        p.alpha = p.opacity * glm::smoothstep(0.0f, 0.08f, t) * (1.0f - glm::smoothstep(0.4f, 1.0f, t));
        if (p.kind == Kind::Spray)
            p.alpha *= glm::smoothstep(-0.1f, p.size, p.position.y - surface); // faint until the quad's lower half is clear of the water
        ++i;
    }
}

// A droplet thrown off one side of the bow. The axes come from the Bow node's world matrix
// (column 0 = port, 1 = up, 2 = forward), so the spray follows the heel and pitch of the boat.
void ParticleSystem::spawnSpray(const glm::mat4& bow, const glm::vec3& bowVelocity, float strength)
{
    if (m_count >= MaxParticles)
        return;
    const glm::vec3 port = glm::normalize(glm::vec3(bow[0]));
    const glm::vec3 up = glm::normalize(glm::vec3(bow[1]));
    const glm::vec3 forward = glm::normalize(glm::vec3(bow[2]));
    const float side = randomRange(0.0f, 1.0f) < 0.5f ? -1.0f : 1.0f; // both sides of the stem
    const float aft = randomRange(0.0f, 1.2f); // the bow wave runs along the first metre of the hull

    Particle& p = m_particles[m_count++];
    p.kind = Kind::Spray;
    // The hull gets wider going aft, so start further out the further aft.
    p.position = glm::vec3(bow[3]) - forward * aft + port * (side * (0.05f + 0.35f * aft));
    // Part of the boat's horizontal motion, then thrown outwards and up; harder impacts throw it further.
    p.velocity = 0.6f * glm::vec3(bowVelocity.x, 0.0f, bowVelocity.z)
        + port * (side * randomRange(1.0f, 2.5f) * strength)
        + up * (randomRange(2.0f, 4.5f) * strength)
        + forward * randomRange(-0.3f, 0.6f);
    p.age = 0.0f;
    p.life = randomRange(0.7f, 1.4f);
    p.startSize = randomRange(0.15f, 0.3f);
    p.endSize = p.startSize * randomRange(1.5f, 2.5f); // droplets spread out into mist
    p.rotation = randomRange(0.0f, glm::two_pi<float>());
    p.opacity = randomRange(0.5f, 0.9f);
    p.size = p.startSize;
    p.alpha = 0.0f;
}

// A patch of foam behind the transom. It lies flat on the water and stays roughly where it was
// made, so a trail forms behind the moving boat.
void ParticleSystem::spawnWake(const glm::mat4& stern, const glm::vec3& sternVelocity)
{
    if (m_count >= MaxParticles)
        return;
    const glm::vec3 port = glm::normalize(glm::vec3(stern[0]));

    Particle& p = m_particles[m_count++];
    p.kind = Kind::Wake;
    const float offset = randomRange(-0.7f, 0.7f); // across the transom
    p.position = glm::vec3(stern[3]) + port * offset;
    // Stirred-up water lags behind the boat (it keeps only a little of the stern's velocity) and
    // spreads sideways, so the trail widens behind the boat.
    p.velocity = 0.2f * glm::vec3(sternVelocity.x, 0.0f, sternVelocity.z) + port * (0.8f * offset);
    p.age = 0.0f;
    p.life = randomRange(2.5f, 4.0f);
    p.startSize = randomRange(0.3f, 0.5f);
    p.endSize = p.startSize * randomRange(2.0f, 3.0f);
    p.rotation = randomRange(0.0f, glm::two_pi<float>());
    p.opacity = randomRange(0.2f, 0.4f);
    p.size = p.startSize;
    p.alpha = 0.0f;
}

void ParticleSystem::draw(const FrameContext& frame)
{
    if (m_count == 0)
        return;

    // ---- 1. One instance per particle, sorted back to front along the view direction. "Over"
    // blending is order dependent: a far puff drawn after a near one would wrongly cover it.
    m_instances.clear();
    for (size_t i = 0; i < m_count; ++i) {
        const Particle& p = m_particles[i];
        m_instances.push_back({ p.position, p.size, p.alpha, p.rotation, p.kind == Kind::Wake ? 1.0f : 0.0f });
    }
    const glm::vec3 eye = frame.camera.position;
    const glm::vec3 forward = frame.camera.forward;
    std::sort(m_instances.begin(), m_instances.end(), [&](const Instance& a, const Instance& b) {
        return glm::dot(a.position - eye, forward) > glm::dot(b.position - eye, forward);
    });

    // ---- 2. Upload. Re-specifying the storage first ("orphaning") lets the driver hand us a fresh
    // buffer, so it doesn't have to wait until the GPU has finished drawing last frame's particles.
    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVbo);
    glBufferData(GL_ARRAY_BUFFER, MaxParticles * sizeof(Instance), nullptr, GL_STREAM_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(m_instances.size() * sizeof(Instance)), m_instances.data());

    // ---- 3. All particles in one instanced draw call
    m_shader.bind();
    uniform::set("viewProjection", frame.camera.projection * frame.camera.view);
    // The camera's axes in world space are the columns of the inverse view matrix; the spray quads are spanned by them.
    const glm::mat4 cameraWorld = glm::inverse(frame.camera.view);
    uniform::set("cameraRight", glm::vec3(cameraWorld[0]));
    uniform::set("cameraUp", glm::vec3(cameraWorld[1]));
    uniform::set("cameraPosition", frame.camera.position);
    uniform::set("lightDirection", frame.lighting.lightDirection);
    uniform::set("lightColor", frame.lighting.lightColor);
    uniform::set("ambientColor", frame.lighting.ambientColor);
    m_puff->bind(GL_TEXTURE0);
    uniform::set("puffTexture", 0);

    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); // premultiplied alpha: the shader outputs (colour * alpha, alpha)
    glBindVertexArray(m_vao);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, static_cast<GLsizei>(m_instances.size()));
    glBindVertexArray(0);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); // what the next transparent pass expects (renderer.h)
}
