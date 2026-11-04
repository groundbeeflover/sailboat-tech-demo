#pragma once
#include "feature_toggles.h"
#include "texture.h"
#include <framework/disable_all_warnings.h>
#include <framework/opengl_includes.h>
#include <framework/shader.h>
DISABLE_WARNINGS_PUSH()
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec2.hpp>
DISABLE_WARNINGS_POP()
#include <cstddef>
#include <memory>
#include <random>
#include <vector>

class Waves;
struct FrameContext;

// Where the boat throws up water this frame. Plain data, so the particle code does not depend on
// the Boat class (the same idea as CameraInputs in camera.h).
struct SprayEmitters {
    glm::mat4 bow { 1.0f }; // world matrix of the "Bow" node: on the stem, at the waterline
    glm::mat4 stern { 1.0f }; // world matrix of the "Stern" node: on the transom, at the waterline
    float boatSpeed { 0.0f }; // m/s, from Boat::telemetry()
};

// Bow spray and a foam wake: a fixed pool of particles simulated on the CPU and drawn as textured
// quads with one instanced draw call.
//
//   update(): spawn (rate ~ boat speed and wave impact at the bow), integrate gravity, drag and
//             wind with the real dt, kill or float on the wave surface. Pure math, no OpenGL.
//   draw():   sort back to front, upload one instance per particle, draw. Runs inside the
//             renderer's transparent pass (RenderLists::transparent), so the HDR target is bound.
//
// The constructor needs an OpenGL context (shader, buffers, texture).
class ParticleSystem {
public:
    static constexpr size_t MaxParticles = 4000;

    ParticleSystem();
    ParticleSystem(const ParticleSystem&) = delete;
    ParticleSystem& operator=(const ParticleSystem&) = delete;
    ~ParticleSystem();

    void update(float dt, float time, const FeatureToggles& toggles, const SprayEmitters& emitters, const Waves& waves);
    void draw(const FrameContext& frame);
    // A splash at `point`, e.g. where the boat hits a buoy (A5): droplets thrown out along `normal`
    // (world xz) and up, more of them and faster for a larger impact speed (m/s).
    void burst(const glm::vec3& point, const glm::vec2& normal, float speed);


    size_t count() const { return m_count; }

private:
    enum class Kind { Spray, Wake };

    struct Particle {
        glm::vec3 position;
        glm::vec3 velocity;
        float age, life; // seconds
        float startSize, endSize; // half-size of the quad in metres, grows over the life
        float rotation; // of the texture on the quad, so the puffs don't all look the same
        float opacity; // peak alpha
        Kind kind;
        // Written by update(), read by draw():
        float size;
        float alpha;
    };

    // What the GPU gets per particle (vertex attributes 1 and 2, advanced once per instance).
    struct Instance {
        glm::vec3 position;
        float size;
        float alpha;
        float rotation;
        float flat; // 1 = lies on the water (wake foam), 0 = faces the camera (spray)
    };

    void spawnSpray(const glm::mat4& bow, const glm::vec3& bowVelocity, float strength);
    void spawnWake(const glm::mat4& stern, const glm::vec3& sternVelocity);
    float randomRange(float lo, float hi);

    std::vector<Particle> m_particles; // the pool: [0, m_count) are alive, the rest is free
    size_t m_count { 0 };
    std::vector<Instance> m_instances; // rebuilt every frame, capacity reserved once

    float m_sprayCredit { 0.0f }; // fractional particles carried over to the next frame
    float m_wakeCredit { 0.0f };
    glm::vec3 m_previousBow { 0.0f };
    glm::vec3 m_previousStern { 0.0f };
    bool m_initialised { false };
    std::mt19937 m_rng { 1234u }; // fixed seed, so --capture screenshots are reproducible

    Shader m_shader;
    std::unique_ptr<Texture> m_puff;
    GLuint m_vao { 0 };
    GLuint m_quadVbo { 0 };
    GLuint m_instanceVbo { 0 };
};
