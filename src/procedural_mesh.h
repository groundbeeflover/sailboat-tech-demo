#pragma once
#include <framework/disable_all_warnings.h>
#include <framework/mesh.h>
DISABLE_WARNINGS_PUSH()
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
DISABLE_WARNINGS_POP()
#include <span>

// Mesh generators. Everything is built in code so the meshes, their UVs and their hierarchy are ours.
// All functions return the framework's Mesh, which GPUMesh uploads (and computes tangents for).

// Hull shape as functions of u in [0, 1] along the length (0 = transom/stern, 1 = bow).
// Boat frame: +z forward, +y up, +x to port, origin on the waterline amidships.
struct HullShape {
    float length { 7.5f };
    float beam { 2.5f }; // maximum width at deck level
    float draft { 0.55f }; // depth of the canoe body below the waterline
    float freeboard { 0.75f }; // deck height above the waterline amidships

    float z(float u) const { return (u - 0.5f) * length; }
    float uAt(float zCoord) const { return zCoord / length + 0.5f; }
    float halfBeam(float u) const; // at deck level
    float depth(float u) const; // keel depth below the waterline
    float sheer(float u) const; // deck edge height above the waterline
};

Mesh makeHull(const HullShape& shape, int stations = 48);
Mesh makeDeck(const HullShape& shape, int stations = 48);

// Box centred on the origin, UVs in metres per face.
Mesh makeBox(const glm::vec3& halfExtents);
// Cylinder (or cone when radii differ) along +y from y = 0 to y = height, with caps.
Mesh makeCylinder(float radiusBottom, float radiusTop, float height, int segments = 24);
// Cylinder from point a to point b.
Mesh makeCylinderBetween(const glm::vec3& a, const glm::vec3& b, float radius, int segments = 12);
Mesh makeSphere(float radius, int segments = 24, int rings = 12);

// Triangular mainsail: luff along +y (on the mast), foot along -z (on the boom).
// It bulges along +x with a camber of `camber` times the local chord, and each horizontal section is
// turned about the luff, from 0 at the foot to `twistDeg` at the head, so the leech swings out to the
// same side (the top of a real sail falls off to leeward). The vertices and triangles depend only on
// foot, luff and resolution, so a flat sail and a full one can be the base and target of a blend shape.
Mesh makeSail(float foot, float luff, float camber = 0.1f, float twistDeg = 0.0f, int resolution = 20);

// Helpers
void transformMesh(Mesh& mesh, const glm::mat4& transform);
Mesh mergeMeshList(std::span<const Mesh> meshes);
void computeSmoothNormals(Mesh& mesh); // area-weighted average of face normals
