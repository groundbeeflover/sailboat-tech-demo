#include "water.h"
#include "procedural_texture.h"
#include <cmath>
#include <cstdint>
#include <vector>

namespace {
// Fraction of the spacing growth that is linear; the rest is quadratic.
constexpr float LinearPart = 0.15f;

// Maps t in [-1, 1] to a distance from the centre: spacing is LinearPart * halfSize * dt at the centre
// and (2 - LinearPart) * halfSize * dt at the edge.
float spread(float t, float halfSize)
{
    return std::copysign(LinearPart * std::abs(t) + (1.0f - LinearPart) * t * t, t) * halfSize;
}
}

Water::Water(int resolution, float halfSize)
    : m_halfSize(halfSize)
{
    const float dt = 2.0f / static_cast<float>(resolution - 1);
    m_snapStep = LinearPart * halfSize * dt; // finest spacing, at the centre

    std::vector<glm::vec2> vertices;
    vertices.reserve(static_cast<size_t>(resolution) * static_cast<size_t>(resolution));
    for (int i = 0; i < resolution; ++i) {
        for (int j = 0; j < resolution; ++j) {
            const float tx = -1.0f + static_cast<float>(j) * dt;
            const float tz = -1.0f + static_cast<float>(i) * dt;
            vertices.emplace_back(spread(tx, halfSize), spread(tz, halfSize));
        }
    }
    std::vector<uint32_t> indices;
    indices.reserve(static_cast<size_t>(resolution - 1) * static_cast<size_t>(resolution - 1) * 6);
    const auto stride = static_cast<uint32_t>(resolution);
    for (uint32_t i = 0; i + 1 < stride; ++i) {
        for (uint32_t j = 0; j + 1 < stride; ++j) {
            const uint32_t a = i * stride + j, b = a + 1, c = a + stride, d = c + 1;
            // Counter-clockwise seen from above (+y).
            indices.insert(indices.end(), { a, c, b, b, c, d });
        }
    }
    m_indexCount = static_cast<GLsizei>(indices.size());

    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);
    glGenBuffers(1, &m_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(glm::vec2)), vertices.data(), GL_STATIC_DRAW);
    glGenBuffers(1, &m_ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size() * sizeof(uint32_t)), indices.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(glm::vec2), nullptr);
    glBindVertexArray(0);

    const ImageRGBA ripples = generateWaterNormals();
    m_detailNormals = std::make_unique<Texture>(ripples.width, ripples.height, ripples.pixels, false);
}

Water::~Water()
{
    glDeleteBuffers(1, &m_ibo);
    glDeleteBuffers(1, &m_vbo);
    glDeleteVertexArrays(1, &m_vao);
}

glm::vec2 Water::gridOffset(const glm::vec3& cameraPosition) const
{
    return glm::vec2(std::floor(cameraPosition.x / m_snapStep) * m_snapStep, std::floor(cameraPosition.z / m_snapStep) * m_snapStep);
}

void Water::draw() const
{
    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr);
}
