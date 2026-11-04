#include "procedural_mesh.h"
#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
DISABLE_WARNINGS_POP()
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace {

constexpr float Pi = glm::pi<float>();

Mesh newMesh()
{
    Mesh mesh;
    mesh.material.kd = glm::vec3(1.0f); // unused by the PBR path, but GPUMesh copies it
    return mesh;
}

Vertex makeVertex(const glm::vec3& position, const glm::vec3& normal, const glm::vec2& uv)
{
    Vertex v;
    v.position = position;
    v.normal = normal;
    v.texCoord = uv;
    return v;
}

// Appends a (rows + 1) x (cols + 1) grid of vertices produced by vertexAt(i, j) plus two triangles per cell.
// Without flip, triangle normals point along d(row) x d(column).
template <typename VertexFn>
void addGrid(Mesh& mesh, int rows, int cols, VertexFn vertexAt, bool flip)
{
    const auto base = static_cast<uint32_t>(mesh.vertices.size());
    for (int i = 0; i <= rows; ++i)
        for (int j = 0; j <= cols; ++j)
            mesh.vertices.push_back(vertexAt(i, j));

    const auto stride = static_cast<uint32_t>(cols + 1);
    for (int i = 0; i < rows; ++i) {
        for (int j = 0; j < cols; ++j) {
            const uint32_t a = base + static_cast<uint32_t>(i) * stride + static_cast<uint32_t>(j); // (i, j)
            const uint32_t b = a + stride; // (i + 1, j)
            const uint32_t c = b + 1; // (i + 1, j + 1)
            const uint32_t d = a + 1; // (i, j + 1)
            if (!flip) {
                mesh.triangles.emplace_back(a, b, c);
                mesh.triangles.emplace_back(a, c, d);
            } else {
                mesh.triangles.emplace_back(a, c, b);
                mesh.triangles.emplace_back(a, d, c);
            }
        }
    }
}

}

// ---------------------------------------------------------------------------------------------
// Hull

float HullShape::halfBeam(float u) const
{
    const float hb = 0.5f * beam;
    if (u < 0.45f) {
        // Aft of the widest point the hull narrows a little to a transom at 70% of the beam.
        const float a = (0.45f - u) / 0.45f;
        return hb * (1.0f - 0.3f * a * a);
    }
    // Forward it narrows to a point at the stem (non-zero slope there gives a sharp bow).
    const float a = (u - 0.45f) / 0.55f;
    return hb * std::max(0.0f, 1.0f - a * a);
}

float HullShape::depth(float u) const
{
    // Deepest just aft of amidships, rising towards both ends.
    return draft * (0.15f + 0.85f * std::pow(std::sin(Pi * (0.1f + 0.85f * u)), 0.8f));
}

float HullShape::sheer(float u) const
{
    // The deck edge rises towards the bow (a "sheer line").
    return freeboard * (0.85f + 0.35f * u * u);
}

Mesh makeHull(const HullShape& shape, int stations)
{
    Mesh mesh = newMesh();

    // Each cross-section ("station") runs from the keel to the deck edge:
    //  - the part below the waterline is a superellipse quadrant (n = 2 would be an ellipse, larger n a harder bilge)
    //  - the topsides above it flare out slightly from the waterline beam to the deck beam
    constexpr int bilgeSteps = 12;
    constexpr int topsideSteps = 4;
    constexpr int sectionSteps = bilgeSteps + topsideSteps;
    constexpr float n = 2.4f;

    // v is the girth texture coordinate: 0 at the keel, 0.5 at the waterline, 1 at the deck edge.
    // The hull texture uses that split to paint antifouling below and varnished planks above.
    auto sectionPoint = [&](float u, int j, float side, float& v) {
        const float hb = shape.halfBeam(u);
        const float waterlineBeam = 0.92f * hb;
        float x, y;
        if (j <= bilgeSteps) {
            const float phi = 0.5f * Pi * static_cast<float>(j) / bilgeSteps;
            x = waterlineBeam * std::pow(std::max(0.0f, std::sin(phi)), 2.0f / n);
            y = -shape.depth(u) * std::pow(std::max(0.0f, std::cos(phi)), 2.0f / n);
            v = 0.5f * static_cast<float>(j) / bilgeSteps;
        } else {
            const float t = static_cast<float>(j - bilgeSteps) / topsideSteps;
            x = waterlineBeam + (hb - waterlineBeam) * t;
            y = shape.sheer(u) * t;
            v = 0.5f + 0.5f * t;
        }
        return glm::vec3(side * x, y, shape.z(u));
    };

    // Two separate grids (starboard, port) so the keel line keeps a crisp edge and each side
    // gets consistent tangent handedness for normal mapping.
    for (float side : { -1.0f, 1.0f }) {
        addGrid(
            mesh, stations, sectionSteps,
            [&](int i, int j) {
                const float u = static_cast<float>(i) / static_cast<float>(stations);
                float v;
                const glm::vec3 p = sectionPoint(u, j, side, v);
                return makeVertex(p, glm::vec3(0.0f), glm::vec2(u, v));
            },
            side > 0.0f); // the port side is mirrored, so its winding must be flipped to face outwards
    }

    // Transom: flat stern panel facing -z, built as horizontal strips between the starboard and port
    // points of each section level, so every strip has the same girth v as the hull sides next to it
    // and the planks and waterline line up around the corner.
    addGrid(
        mesh, sectionSteps, 1,
        [&](int j, int column) {
            float v;
            const glm::vec3 p = sectionPoint(0.0f, j, column == 0 ? -1.0f : 1.0f, v);
            return makeVertex(p, glm::vec3(0.0f), glm::vec2(0.5f + p.x / shape.length, v));
        },
        false);

    computeSmoothNormals(mesh);
    return mesh;
}

Mesh makeDeck(const HullShape& shape, int stations)
{
    Mesh mesh = newMesh();
    constexpr int across = 10;
    addGrid(
        mesh, stations, across,
        [&](int i, int j) {
            const float u = static_cast<float>(i) / static_cast<float>(stations);
            const float t = 2.0f * static_cast<float>(j) / across - 1.0f; // -1 starboard edge, +1 port edge
            const float hb = shape.halfBeam(u);
            const float camber = 0.06f * (hb / (0.5f * shape.beam)) * (1.0f - t * t); // deck crowns in the middle
            const glm::vec3 p(t * hb, shape.sheer(u) + camber, shape.z(u));
            // UVs in metres, planks run along v (= along the boat).
            return makeVertex(p, glm::vec3(0.0f), glm::vec2(p.x, p.z));
        },
        false);
    computeSmoothNormals(mesh);
    return mesh;
}

// ---------------------------------------------------------------------------------------------
// Primitives

Mesh makeBox(const glm::vec3& halfExtents)
{
    struct Face {
        glm::vec3 normal, u, v; // cross(u, v) == normal, so the corner order below is counter-clockwise from outside
    };
    const Face faces[6] = {
        { { 1, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 } },
        { { -1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 } },
        { { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, -1 } },
        { { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } },
        { { 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 } },
        { { 0, 0, -1 }, { -1, 0, 0 }, { 0, 1, 0 } },
    };
    Mesh mesh = newMesh();
    for (const Face& f : faces) {
        const float hn = glm::dot(glm::abs(f.normal), halfExtents);
        const float hu = glm::dot(glm::abs(f.u), halfExtents);
        const float hv = glm::dot(glm::abs(f.v), halfExtents);
        const auto base = static_cast<uint32_t>(mesh.vertices.size());
        const glm::vec2 corners[4] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
        for (const glm::vec2& c : corners) {
            const glm::vec3 p = f.normal * hn + f.u * (c.x * hu) + f.v * (c.y * hv);
            mesh.vertices.push_back(makeVertex(p, f.normal, glm::vec2(c.x * hu, c.y * hv)));
        }
        mesh.triangles.emplace_back(base, base + 1, base + 2);
        mesh.triangles.emplace_back(base, base + 2, base + 3);
    }
    return mesh;
}

Mesh makeCylinder(float radiusBottom, float radiusTop, float height, int segments)
{
    Mesh mesh = newMesh();
    const float slope = (radiusBottom - radiusTop) / height; // makes cone normals tilt upwards
    // Side: rows go up, columns go around (theta from +x towards +z), so d(row) x d(col) points outwards.
    addGrid(
        mesh, 1, segments,
        [&](int i, int j) {
            const float theta = 2.0f * Pi * static_cast<float>(j) / static_cast<float>(segments);
            const float y = static_cast<float>(i) * height;
            const float r = i == 0 ? radiusBottom : radiusTop;
            const glm::vec3 dir(std::cos(theta), 0.0f, std::sin(theta));
            const glm::vec3 normal = glm::normalize(glm::vec3(dir.x, slope, dir.z));
            return makeVertex(dir * r + glm::vec3(0.0f, y, 0.0f), normal, glm::vec2(static_cast<float>(j) / static_cast<float>(segments), y));
        },
        false);

    // Caps
    for (int cap = 0; cap < 2; ++cap) {
        const float y = cap == 0 ? 0.0f : height;
        const float r = cap == 0 ? radiusBottom : radiusTop;
        if (r <= 0.0f)
            continue;
        const glm::vec3 normal(0.0f, cap == 0 ? -1.0f : 1.0f, 0.0f);
        const auto base = static_cast<uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back(makeVertex(glm::vec3(0.0f, y, 0.0f), normal, glm::vec2(0.0f)));
        for (int k = 0; k < segments; ++k) {
            const float theta = 2.0f * Pi * static_cast<float>(k) / static_cast<float>(segments);
            const glm::vec3 p(r * std::cos(theta), y, r * std::sin(theta));
            mesh.vertices.push_back(makeVertex(p, normal, glm::vec2(p.x, p.z)));
        }
        for (int k = 0; k < segments; ++k) {
            const uint32_t current = base + 1 + static_cast<uint32_t>(k);
            const uint32_t next = base + 1 + static_cast<uint32_t>((k + 1) % segments);
            if (cap == 0)
                mesh.triangles.emplace_back(base, current, next);
            else
                mesh.triangles.emplace_back(base, next, current);
        }
    }
    return mesh;
}

Mesh makeCylinderBetween(const glm::vec3& a, const glm::vec3& b, float radius, int segments)
{
    const glm::vec3 axis = b - a;
    const float length = glm::length(axis);
    Mesh mesh = makeCylinder(radius, radius, length, segments);
    // Right-handed frame whose y axis is the cylinder axis.
    const glm::vec3 y = axis / length;
    const glm::vec3 helper = std::abs(y.y) < 0.99f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    const glm::vec3 x = glm::normalize(glm::cross(y, helper));
    const glm::vec3 z = glm::cross(x, y);
    glm::mat4 frame(1.0f);
    frame[0] = glm::vec4(x, 0.0f);
    frame[1] = glm::vec4(y, 0.0f);
    frame[2] = glm::vec4(z, 0.0f);
    frame[3] = glm::vec4(a, 1.0f);
    transformMesh(mesh, frame);
    return mesh;
}

Mesh makeSphere(float radius, int segments, int rings)
{
    Mesh mesh = newMesh();
    addGrid(
        mesh, rings, segments,
        [&](int i, int j) {
            const float phi = Pi * (1.0f - static_cast<float>(i) / static_cast<float>(rings)); // bottom to top
            const float theta = 2.0f * Pi * static_cast<float>(j) / static_cast<float>(segments);
            const glm::vec3 n(std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta));
            return makeVertex(n * radius, n, glm::vec2(static_cast<float>(j) / static_cast<float>(segments), static_cast<float>(i) / static_cast<float>(rings)));
        },
        false);
    return mesh;
}

Mesh makeSail(float foot, float luff, float camber, float twistDeg, int resolution)
{
    Mesh mesh = newMesh();
    addGrid(
        mesh, resolution, resolution,
        [&](int i, int j) {
            const float t = static_cast<float>(i) / static_cast<float>(resolution); // up the luff
            const float s = static_cast<float>(j) / static_cast<float>(resolution); // along the chord, luff (0) to leech (1)
            const float chord = (1.0f - t) * foot; // triangle: the top row collapses into the head
            // Chord-wise camber profile with its deepest point at about 40% of the chord (like a real sail).
            const float profile = 0.5f * (4.0f * s * (1.0f - s)) + 0.5f * (6.75f * s * (1.0f - s) * (1.0f - s));
            const glm::vec3 p(camber * chord * profile, t * luff, -s * chord);
            // Twist: rotate the section about the luff (the y axis), more towards the head. The leech
            // has z < 0, so it swings out to +x, the side of the bulge.
            const float twist = glm::radians(twistDeg) * t;
            const glm::vec3 twisted(p.x * std::cos(twist) - p.z * std::sin(twist), p.y, p.x * std::sin(twist) + p.z * std::cos(twist));
            return makeVertex(twisted, glm::vec3(0.0f), glm::vec2(s, t));

        },
        true);
    computeSmoothNormals(mesh);
    return mesh;
}

// ---------------------------------------------------------------------------------------------
// Helpers

void transformMesh(Mesh& mesh, const glm::mat4& transform)
{
    const glm::mat3 normalMatrix = glm::inverseTranspose(glm::mat3(transform));
    for (Vertex& v : mesh.vertices) {
        v.position = glm::vec3(transform * glm::vec4(v.position, 1.0f));
        v.normal = glm::normalize(normalMatrix * v.normal);
    }
}

Mesh mergeMeshList(std::span<const Mesh> meshes)
{
    Mesh result = newMesh();
    for (const Mesh& m : meshes) {
        const auto offset = static_cast<uint32_t>(result.vertices.size());
        result.vertices.insert(result.vertices.end(), m.vertices.begin(), m.vertices.end());
        for (const glm::uvec3& tri : m.triangles)
            result.triangles.push_back(tri + glm::uvec3(offset));
    }
    return result;
}

void computeSmoothNormals(Mesh& mesh)
{
    for (Vertex& v : mesh.vertices)
        v.normal = glm::vec3(0.0f);
    // The un-normalised cross product is twice the triangle area, so larger faces weigh more.
    for (const glm::uvec3& tri : mesh.triangles) {
        const glm::vec3 faceNormal = glm::cross(mesh.vertices[tri.y].position - mesh.vertices[tri.x].position,
            mesh.vertices[tri.z].position - mesh.vertices[tri.x].position);
        mesh.vertices[tri.x].normal += faceNormal;
        mesh.vertices[tri.y].normal += faceNormal;
        mesh.vertices[tri.z].normal += faceNormal;
    }
    for (Vertex& v : mesh.vertices) {
        const float len = glm::length(v.normal);
        v.normal = len > 1e-12f ? v.normal / len : glm::vec3(0.0f, 1.0f, 0.0f);
    }
}
